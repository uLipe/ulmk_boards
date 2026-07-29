/* SPDX-License-Identifier: MIT */
/*
 * GPSPI2 master — CPU FIFO full-duplex.  Does not touch MSPI (PSRAM/flash).
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "spi.h"
#include "board_config.h"
#include "pinmux_internal.h"

#define SPI_STACK		1536u

#define SPI2_BASE		ULMK_BOARD_SPI2_BASE
#define SPI_CMD			(SPI2_BASE + 0x00u)
#define SPI_CTRL		(SPI2_BASE + 0x08u)
#define SPI_CLOCK		(SPI2_BASE + 0x0cu)
#define SPI_USER		(SPI2_BASE + 0x10u)
#define SPI_USER1		(SPI2_BASE + 0x14u)
#define SPI_USER2		(SPI2_BASE + 0x18u)
#define SPI_MS_DLEN		(SPI2_BASE + 0x1cu)
#define SPI_MISC		(SPI2_BASE + 0x20u)
#define SPI_DMA_CONF		(SPI2_BASE + 0x30u)
#define SPI_DMA_INT_CLR		(SPI2_BASE + 0x38u)
#define SPI_DMA_INT_RAW		(SPI2_BASE + 0x3cu)
#define SPI_SLAVE		(SPI2_BASE + 0xe0u)
#define SPI_CLK_GATE		(SPI2_BASE + 0xe8u)
#define SPI_W0			(SPI2_BASE + 0x98u)

#define SPI_CMD_UPDATE		(1u << 23)
#define SPI_CMD_USR		(1u << 24)
/* P4: bit0=infifo_err; trans_done is bit12 */
#define SPI_INT_TRANS_DONE	(1u << 12)
#define SPI_CLK_EN		(1u << 0)
#define SPI_MST_CLK_ACTIVE	(1u << 1)
#define SPI_RX_AFIFO_RST	(1u << 29)
#define SPI_BUF_AFIFO_RST	(1u << 30)

#define SPI2_CK_OUT		53u
#define SPI2_Q_IN		54u /* MISO */
#define SPI2_D_OUT		55u /* MOSI */
#define SPI2_CS_OUT		62u

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL1	(HP_CLKRST + 0x18u)
#define HP_SOC_CLK_CTRL2	(HP_CLKRST + 0x1cu)
#define HP_PERI_CLK_CTRL116	(HP_CLKRST + 0x80u)
#define HP_RST_EN2		(HP_CLKRST + 0xc8u)

#define GPSPI2_SYS_CLK_EN	(1u << 0)   /* soc_clk_ctrl1 */
#define GPSPI2_APB_EN		(1u << 19)  /* soc_clk_ctrl2 */
#define GPSPI2_CLK_SRC_MASK	(0x7u << 0) /* peri_clk_ctrl116[2:0] XTAL=0 */
#define GPSPI2_HS_CLK_EN	(1u << 3)
#define GPSPI2_HS_DIV_SHIFT	4
#define GPSPI2_MST_DIV_SHIFT	12
#define GPSPI2_MST_CLK_EN	(1u << 20)
#define RST_SPI2		(1u << 7)

#ifndef ULMK_BOARD_SPI_MAX
#define ULMK_BOARD_SPI_MAX	1u
#endif

ulmk_ep_t g_spi_eps[ULMK_BOARD_SPI_MAX];
static uint8_t g_ready;

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void spi_clk_enable(void)
{
	uint32_t v;

	/* Match IDF spi_ll: sys on ctrl1, apb on ctrl2. */
	v = rd(HP_SOC_CLK_CTRL1);
	wr(HP_SOC_CLK_CTRL1, v | GPSPI2_SYS_CLK_EN);

	v = rd(HP_SOC_CLK_CTRL2);
	wr(HP_SOC_CLK_CTRL2, v | GPSPI2_APB_EN);

	/*
	 * Functional clock: XTAL src, hs_div=1, mst_div=2 (IDF minimum),
	 * then enable hs + mst gates.
	 */
	v = rd(HP_PERI_CLK_CTRL116);
	v &= ~(GPSPI2_CLK_SRC_MASK |
	       (0xFFu << GPSPI2_HS_DIV_SHIFT) |
	       (0xFFu << GPSPI2_MST_DIV_SHIFT));
	v |= (0u << 0) | /* XTAL */
	     GPSPI2_HS_CLK_EN |
	     (0u << GPSPI2_HS_DIV_SHIFT) |   /* hs_div-1 = 0 */
	     (1u << GPSPI2_MST_DIV_SHIFT) |  /* mst_div-1 = 1 → /2 */
	     GPSPI2_MST_CLK_EN;
	wr(HP_PERI_CLK_CTRL116, v);

	v = rd(HP_RST_EN2);
	wr(HP_RST_EN2, v | RST_SPI2);
	wr(HP_RST_EN2, v & ~RST_SPI2);
}

static int spi_wait_clear(uint32_t addr, uint32_t bit, uint32_t spins_max)
{
	uint32_t spins = 0u;

	while ((rd(addr) & bit) != 0u) {
		if (++spins > spins_max)
			return ULMK_ETIMEOUT;
	}
	return ULMK_OK;
}

static int spi_wait_set(uint32_t addr, uint32_t bit, uint32_t spins_max)
{
	uint32_t spins = 0u;

	while ((rd(addr) & bit) == 0u) {
		if (++spins > spins_max)
			return ULMK_ETIMEOUT;
	}
	return ULMK_OK;
}

static void spi_apply_config(void)
{
	wr(SPI_CMD, SPI_CMD_UPDATE);
	(void)spi_wait_clear(SPI_CMD, SPI_CMD_UPDATE, 100000u);
}

static void spi_pins(int loopback)
{
	pinmux_cfg_t cfg = {0};

	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_PERIPH_OE;

	cfg.pin = ULMK_BOARD_SPI2_SCLK_GPIO;
	cfg.matrix_out = SPI2_CK_OUT;
	cfg.matrix_in = 0u;
	(void)pinmux_apply(&cfg);

	cfg.pin = ULMK_BOARD_SPI2_MOSI_GPIO;
	cfg.matrix_out = SPI2_D_OUT;
	/* IE needed so matrix can sample MOSI for soft loopback. */
	if (loopback)
		cfg.flags = PINMUX_F_PERIPH_OE | PINMUX_F_IE;
	(void)pinmux_apply(&cfg);
	cfg.flags = PINMUX_F_PERIPH_OE;

	cfg.pin = ULMK_BOARD_SPI2_CS_GPIO;
	cfg.matrix_out = SPI2_CS_OUT;
	(void)pinmux_apply(&cfg);

	if (loopback) {
		/*
		 * Soft loop: SPI2_Q (MISO) samples the MOSI pad via matrix
		 * (no physical jumper).  Skip programming MISO GPIO OE.
		 */
		*(volatile uint32_t *)(ULMK_BOARD_GPIO_BASE + 0x158u +
				      4u * SPI2_Q_IN) =
			(ULMK_BOARD_SPI2_MOSI_GPIO & 0x3Fu) | (1u << 7);
	} else {
		cfg.pin = ULMK_BOARD_SPI2_MISO_GPIO;
		cfg.dir = PINMUX_DIR_IN;
		cfg.flags = PINMUX_F_IE;
		cfg.matrix_out = 0u;
		cfg.matrix_in = SPI2_Q_IN;
		(void)pinmux_apply(&cfg);
	}
}

static void spi_hw_init(void)
{
	uint32_t clk;

	spi_clk_enable();

	/* Power on SPI module clock (defaults to off). */
	wr(SPI_CLK_GATE, SPI_CLK_EN | SPI_MST_CLK_ACTIVE);

	wr(SPI_SLAVE, 0u);
	wr(SPI_USER1, 0u);
	wr(SPI_USER2, 0u);
	wr(SPI_USER, 0u);
	wr(SPI_CTRL, 0u);
	wr(SPI_MISC, 0u);
	wr(SPI_DMA_CONF, 0u);

	/*
	 * ~1 MHz from functional clk (~20 MHz after mst/2 on XTAL40):
	 * clkcnt_n=19 → f/(n+1); clk_equ_sysclk=0 so divider applies.
	 */
	clk = (19u << 0) | (9u << 6) | (19u << 12) | (0u << 18);
	wr(SPI_CLOCK, clk);

	/* Full duplex, MOSI+MISO */
	wr(SPI_USER, (1u << 0) | /* doutdin */
		     (1u << 27) | /* usr_mosi */
		     (1u << 28)); /* usr_miso */

	spi_apply_config();
	g_ready = 1u;
}

static int spi_xfer_hw(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	uint32_t words;
	uint32_t i;
	uint32_t w;
	uint32_t bits;
	int rc;

	if (!g_ready || len == 0u || len > 64u)
		return ULMK_EINVAL;

	bits = len * 8u;
	wr(SPI_MS_DLEN, bits - 1u);

	words = (len + 3u) / 4u;
	for (i = 0u; i < words; i++) {
		w = 0u;
		if (tx) {
			uint32_t b0 = (i * 4u < len) ? tx[i * 4u] : 0u;
			uint32_t b1 = (i * 4u + 1u < len) ? tx[i * 4u + 1u] : 0u;
			uint32_t b2 = (i * 4u + 2u < len) ? tx[i * 4u + 2u] : 0u;
			uint32_t b3 = (i * 4u + 3u < len) ? tx[i * 4u + 3u] : 0u;
			w = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
		}
		wr(SPI_W0 + i * 4u, w);
	}

	/* Reset CPU FIFOs (self-clearing pulses). */
	wr(SPI_DMA_CONF, SPI_BUF_AFIFO_RST | SPI_RX_AFIFO_RST);
	wr(SPI_DMA_CONF, 0u);

	wr(SPI_DMA_INT_CLR, SPI_INT_TRANS_DONE);
	/* P4: apply config, then start — separate domain sync. */
	spi_apply_config();
	wr(SPI_CMD, SPI_CMD_USR);

	rc = spi_wait_set(SPI_DMA_INT_RAW, SPI_INT_TRANS_DONE, 500000u);
	if (rc != ULMK_OK)
		return rc;
	wr(SPI_DMA_INT_CLR, SPI_INT_TRANS_DONE);

	if (rx) {
		for (i = 0u; i < words; i++) {
			w = rd(SPI_W0 + i * 4u);
			if (i * 4u < len)
				rx[i * 4u] = (uint8_t)(w & 0xFFu);
			if (i * 4u + 1u < len)
				rx[i * 4u + 1u] = (uint8_t)((w >> 8) & 0xFFu);
			if (i * 4u + 2u < len)
				rx[i * 4u + 2u] = (uint8_t)((w >> 16) & 0xFFu);
			if (i * 4u + 3u < len)
				rx[i * 4u + 3u] = (uint8_t)((w >> 24) & 0xFFu);
		}
	}
	return ULMK_OK;
}

#define SPI_MSG_XFER	1u
#define SPI_MSG_LOOP	2u
#define SPI_IPC_MAX	16u

static void spi_server(void *arg)
{
	ulmk_ep_t ep = (ulmk_ep_t)(uintptr_t)arg;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t local_tx[SPI_IPC_MAX];
	uint8_t local_rx[SPI_IPC_MAX];
	uint32_t i;
	uint32_t len;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;
		reply.words[4] = 0u;
		reply.words[5] = 0u;
		if (msg.label == SPI_MSG_XFER || msg.label == SPI_MSG_LOOP) {
			int rc;

			/* words[0]=len; words[1]=unused; words[2..5]=payload */
			len = msg.words[0];
			if (len == 0u || len > SPI_IPC_MAX) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
				ulmk_ep_reply(sender, &reply);
				continue;
			}
			for (i = 0u; i < len; i++) {
				local_tx[i] = (uint8_t)((msg.words[2u + i / 4u] >>
							 ((i % 4u) * 8u)) &
							0xFFu);
				local_rx[i] = 0u;
			}

			if (msg.label == SPI_MSG_LOOP)
				spi_pins(1);
			rc = spi_xfer_hw(local_tx, local_rx, len);
			reply.words[0] = (uint32_t)rc;
			if (rc == ULMK_OK) {
				for (i = 0u; i < len; i++)
					reply.words[1u + i / 4u] |=
						((uint32_t)local_rx[i] <<
						 ((i % 4u) * 8u));
			}
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t spi_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;

	if (n >= ULMK_BOARD_SPI_MAX)
		return ULMK_TID_INVALID;
	if (g_spi_eps[n] != ULMK_EP_INVALID && g_spi_eps[n] != 0)
		return ULMK_TID_INVALID;

	spi_pins(1);
	spi_hw_init();

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name       = "spi";
	attr.entry      = spi_server;
	attr.arg        = (void *)(uintptr_t)ep;
	attr.priority   = 2u;
	attr.stack_size = SPI_STACK;
	attr.privilege  = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	g_spi_eps[n] = ep;
	return tid;
}

int spi_transfer(uint8_t n, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	ulmk_msg_t msg;
	uint32_t i;
	int rc;

	if (n >= ULMK_BOARD_SPI_MAX ||
	    g_spi_eps[n] == ULMK_EP_INVALID || g_spi_eps[n] == 0)
		return ULMK_ESRCH;
	if (len == 0u || len > SPI_IPC_MAX)
		return ULMK_EINVAL;
	msg.label = SPI_MSG_XFER;
	msg.words[0] = len;
	msg.words[1] = 0u;
	for (i = 0u; i < 4u; i++)
		msg.words[2u + i] = 0u;
	for (i = 0u; i < len; i++)
		msg.words[2u + i / 4u] |=
			((uint32_t)(tx ? tx[i] : 0u) << ((i % 4u) * 8u));
	rc = ulmk_ep_call(g_spi_eps[n], &msg);
	if (rc != ULMK_OK)
		return rc;
	rc = (int)(int32_t)msg.words[0];
	if (rc == ULMK_OK && rx) {
		for (i = 0u; i < len; i++)
			rx[i] = (uint8_t)((msg.words[1u + i / 4u] >>
					   ((i % 4u) * 8u)) & 0xFFu);
	}
	return rc;
}

int spi_loopback(uint8_t n, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	ulmk_msg_t msg;
	uint32_t i;
	int rc;

	if (n >= ULMK_BOARD_SPI_MAX ||
	    g_spi_eps[n] == ULMK_EP_INVALID || g_spi_eps[n] == 0)
		return ULMK_ESRCH;
	if (len == 0u || len > SPI_IPC_MAX)
		return ULMK_EINVAL;
	msg.label = SPI_MSG_LOOP;
	msg.words[0] = len;
	msg.words[1] = 0u;
	for (i = 0u; i < 4u; i++)
		msg.words[2u + i] = 0u;
	for (i = 0u; i < len; i++)
		msg.words[2u + i / 4u] |=
			((uint32_t)(tx ? tx[i] : 0u) << ((i % 4u) * 8u));
	rc = ulmk_ep_call(g_spi_eps[n], &msg);
	if (rc != ULMK_OK)
		return rc;
	rc = (int)(int32_t)msg.words[0];
	if (rc == ULMK_OK && rx) {
		for (i = 0u; i < len; i++)
			rx[i] = (uint8_t)((msg.words[1u + i / 4u] >>
					   ((i % 4u) * 8u)) & 0xFFu);
	}
	return rc;
}
