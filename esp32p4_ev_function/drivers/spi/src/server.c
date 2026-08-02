/* SPDX-License-Identifier: MIT */
/*
 * GPSPI2/3 masters — full-duplex via AXI-PDMA (not AHB or MSPI).
 * Each instance owns one server thread, endpoint and DMA channel pair.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "spi.h"
#include "gdma_axi.h"
#include "board_config.h"
#include "pinmux_internal.h"
#include "board_console.h"

#define SPI_STACK		1536u

#define SPI_CMD			0x00u
#define SPI_CTRL		0x08u
#define SPI_CLOCK		0x0cu
#define SPI_USER		0x10u
#define SPI_USER1		0x14u
#define SPI_USER2		0x18u
#define SPI_MS_DLEN		0x1cu
#define SPI_MISC		0x20u
#define SPI_DMA_CONF		0x30u
#define SPI_SLAVE		0xe0u
#define SPI_CLK_GATE		0xe8u

#define SPI_CMD_UPDATE		(1u << 23)
#define SPI_CMD_USR		(1u << 24)
#define SPI_CLK_EN		(1u << 0)
#define SPI_MST_CLK_ACTIVE	(1u << 1)
#define SPI_RX_AFIFO_RST	(1u << 29)
#define SPI_BUF_AFIFO_RST	(1u << 30)
#define SPI_DMA_RX_ENA		(1u << 27)
#define SPI_DMA_TX_ENA		(1u << 28)
#define SPI_RX_EOF_EN		(1u << 21)

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL1	(HP_CLKRST + 0x18u)
#define HP_SOC_CLK_CTRL2	(HP_CLKRST + 0x1cu)
#define HP_PERI_CLK_CTRL116	(HP_CLKRST + 0x80u)
#define HP_PERI_CLK_CTRL117	(HP_CLKRST + 0x84u)
#define HP_RST_EN2		(HP_CLKRST + 0xc8u)

struct spi_hw {
	uint32_t	base;
	uint32_t	clk_reg;
	uint8_t		sclk_gpio;
	uint8_t		mosi_gpio;
	uint8_t		miso_gpio;
	uint8_t		cs_gpio;
	uint8_t		clk_out;
	uint8_t		q_in;
	uint8_t		d_out;
	uint8_t		cs_out;
	uint8_t		sys_clk_bit;
	uint8_t		apb_clk_bit;
	uint8_t		rst_bit;
	uint8_t		clk_src_shift;
	uint8_t		hs_clk_bit;
	uint8_t		hs_div_shift;
	uint8_t		mst_div_shift;
	uint8_t		mst_clk_bit;
};

struct spi_inst {
	const struct spi_hw	*hw;
	uint8_t			rx_slot;
	uint8_t			tx_slot;
	uint8_t			ready;
};

static const struct spi_hw g_spi_hw[ULMK_BOARD_SPI_MAX] = {
	{
		.base = ULMK_BOARD_SPI2_BASE,
		.clk_reg = HP_PERI_CLK_CTRL116,
		.sclk_gpio = ULMK_BOARD_SPI2_SCLK_GPIO,
		.mosi_gpio = ULMK_BOARD_SPI2_MOSI_GPIO,
		.miso_gpio = ULMK_BOARD_SPI2_MISO_GPIO,
		.cs_gpio = ULMK_BOARD_SPI2_CS_GPIO,
		.clk_out = 53u,
		.q_in = 54u,
		.d_out = 55u,
		.cs_out = 62u,
		.sys_clk_bit = 0u,
		.apb_clk_bit = 19u,
		.rst_bit = 7u,
		.clk_src_shift = 0u,
		.hs_clk_bit = 3u,
		.hs_div_shift = 4u,
		.mst_div_shift = 12u,
		.mst_clk_bit = 20u,
	},
	{
		.base = ULMK_BOARD_SPI3_BASE,
		.clk_reg = HP_PERI_CLK_CTRL117,
		.sclk_gpio = ULMK_BOARD_SPI3_SCLK_GPIO,
		.mosi_gpio = ULMK_BOARD_SPI3_MOSI_GPIO,
		.miso_gpio = ULMK_BOARD_SPI3_MISO_GPIO,
		.cs_gpio = ULMK_BOARD_SPI3_CS_GPIO,
		.clk_out = 47u,
		.q_in = 48u,
		.d_out = 49u,
		.cs_out = 52u,
		.sys_clk_bit = 1u,
		.apb_clk_bit = 20u,
		.rst_bit = 8u,
		.clk_src_shift = 21u,
		.hs_clk_bit = 24u,
		.hs_div_shift = 0u,
		.mst_div_shift = 8u,
		.mst_clk_bit = 16u,
	},
};

ulmk_ep_t g_spi_eps[ULMK_BOARD_SPI_MAX];
static struct spi_inst g_spi[ULMK_BOARD_SPI_MAX];

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static inline uint32_t spi_reg(const struct spi_inst *spi, uint32_t offset)
{
	return spi->hw->base + offset;
}

static void spi_clk_enable(struct spi_inst *spi)
{
	const struct spi_hw *hw = spi->hw;
	uint32_t v;

	v = rd(HP_SOC_CLK_CTRL1);
	wr(HP_SOC_CLK_CTRL1, v | (1u << hw->sys_clk_bit));

	v = rd(HP_SOC_CLK_CTRL2);
	wr(HP_SOC_CLK_CTRL2, v | (1u << hw->apb_clk_bit));

	/*
	 * Functional clock: XTAL src, hs_div=1, mst_div=2 (IDF minimum),
	 * then enable hs + mst gates.
	 */
	v = rd(hw->clk_reg);
	v &= ~((0x7u << hw->clk_src_shift) |
	       (0xFFu << hw->hs_div_shift) |
	       (0xFFu << hw->mst_div_shift));
	v |= (1u << hw->hs_clk_bit) |
	     (1u << hw->mst_div_shift) |
	     (1u << hw->mst_clk_bit);
	wr(hw->clk_reg, v);

	v = rd(HP_RST_EN2);
	wr(HP_RST_EN2, v | (1u << hw->rst_bit));
	wr(HP_RST_EN2, v & ~(1u << hw->rst_bit));
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

static void spi_apply_config(struct spi_inst *spi)
{
	wr(spi_reg(spi, SPI_CMD), SPI_CMD_UPDATE);
	(void)spi_wait_clear(spi_reg(spi, SPI_CMD), SPI_CMD_UPDATE,
			     100000u);
}

static void spi_pins(struct spi_inst *spi, int loopback)
{
	const struct spi_hw *hw = spi->hw;
	pinmux_cfg_t cfg = {0};

	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_PERIPH_OE;

	cfg.pin = hw->sclk_gpio;
	cfg.matrix_out = hw->clk_out;
	cfg.matrix_in = 0u;
	(void)pinmux_apply(&cfg);

	cfg.pin = hw->mosi_gpio;
	cfg.matrix_out = hw->d_out;
	/* IE needed so matrix can sample MOSI for soft loopback. */
	if (loopback)
		cfg.flags = PINMUX_F_PERIPH_OE | PINMUX_F_IE;
	(void)pinmux_apply(&cfg);
	cfg.flags = PINMUX_F_PERIPH_OE;

	cfg.pin = hw->cs_gpio;
	cfg.matrix_out = hw->cs_out;
	(void)pinmux_apply(&cfg);

	if (loopback) {
		/*
		 * Soft loop: SPI2_Q (MISO) samples the MOSI pad via matrix
		 * (no physical jumper).  Skip programming MISO GPIO OE.
		 */
		*(volatile uint32_t *)(ULMK_BOARD_GPIO_BASE + 0x158u +
				      4u * hw->q_in) =
			(hw->mosi_gpio & 0x3Fu) | (1u << 7);
	} else {
		cfg.pin = hw->miso_gpio;
		cfg.dir = PINMUX_DIR_IN;
		cfg.flags = PINMUX_F_IE;
		cfg.matrix_out = 0u;
		cfg.matrix_in = hw->q_in;
		(void)pinmux_apply(&cfg);
	}
}

static void spi_hw_init(struct spi_inst *spi)
{
	uint32_t clk;

	spi_clk_enable(spi);

	wr(spi_reg(spi, SPI_CLK_GATE), SPI_CLK_EN | SPI_MST_CLK_ACTIVE);

	wr(spi_reg(spi, SPI_SLAVE), 0u);
	wr(spi_reg(spi, SPI_USER1), 0u);
	wr(spi_reg(spi, SPI_USER2), 0u);
	wr(spi_reg(spi, SPI_USER), 0u);
	wr(spi_reg(spi, SPI_CTRL), 0u);
	wr(spi_reg(spi, SPI_MISC), 0u);
	wr(spi_reg(spi, SPI_DMA_CONF), 0u);

	/*
	 * ~1 MHz from functional clk (~20 MHz after mst/2 on XTAL40):
	 * clkcnt_n=19 → f/(n+1); clk_equ_sysclk=0 so divider applies.
	 */
	clk = (19u << 0) | (9u << 6) | (19u << 12) | (0u << 18);
	wr(spi_reg(spi, SPI_CLOCK), clk);

	/* Full duplex, MOSI+MISO */
	wr(spi_reg(spi, SPI_USER), (1u << 0) | /* doutdin */
				      (1u << 27) | /* usr_mosi */
				      (1u << 28)); /* usr_miso */

	spi_apply_config(spi);
	spi->ready = 1u;
}

static int spi_xfer_hw(struct spi_inst *spi, const uint8_t *tx,
		       uint8_t *rx, uint32_t len)
{
	uint32_t bits;
	int rc;
	int got;

	if (!spi->ready || !tx || !rx || len == 0u || len > 64u)
		return ULMK_EINVAL;

	bits = len * 8u;
	wr(spi_reg(spi, SPI_MS_DLEN), bits - 1u);

	wr(spi_reg(spi, SPI_DMA_CONF),
	   SPI_BUF_AFIFO_RST | SPI_RX_AFIFO_RST);
	wr(spi_reg(spi, SPI_DMA_CONF),
	   SPI_DMA_RX_ENA | SPI_DMA_TX_ENA | SPI_RX_EOF_EN);

	rc = gdma_axi_rx_arm(spi->rx_slot, rx, len);
	if (rc != ULMK_OK)
		return rc;
	rc = gdma_axi_tx_arm(spi->tx_slot, tx, len);
	if (rc != ULMK_OK)
		return rc;

	spi_apply_config(spi);
	wr(spi_reg(spi, SPI_CMD), SPI_CMD_USR);

	rc = gdma_axi_tx_wait(spi->tx_slot);
	got = gdma_axi_rx_wait(spi->rx_slot);
	wr(spi_reg(spi, SPI_DMA_CONF), 0u);

	if (rc != ULMK_OK)
		return rc;
	if (got < 0)
		return got;
	(void)len;
	return ULMK_OK;
}

#define SPI_MSG_XFER	1u
#define SPI_MSG_LOOP	2u
#define SPI_IPC_MAX	16u

/*
 * DMA buffers get a cache line to themselves.  gdma_axi cleans the source and
 * invalidates the destination around every transfer, and those operations act
 * on the whole line — as stack arrays they took the caller's frame with them.
 * Reach them through the cached alias only: the writeback would otherwise put
 * a stale line back over whatever the uncached alias had just stored.
 */
#define SPI_DMA_LINE	64u

static uint8_t g_dma_tx[ULMK_BOARD_SPI_MAX][SPI_DMA_LINE]
	__attribute__((aligned(SPI_DMA_LINE)));
static uint8_t g_dma_rx[ULMK_BOARD_SPI_MAX][SPI_DMA_LINE]
	__attribute__((aligned(SPI_DMA_LINE)));

static void spi_server(void *arg)
{
	uint8_t n = (uint8_t)(uintptr_t)arg;
	struct spi_inst *spi = &g_spi[n];
	ulmk_ep_t ep = g_spi_eps[n];
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t *local_tx = g_dma_tx[n];
	uint8_t *local_rx = g_dma_rx[n];
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
				spi_pins(spi, 1);
			rc = spi_xfer_hw(spi, g_dma_tx[n], g_dma_rx[n], len);
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
	struct spi_inst *spi;
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	uint32_t periph;

	if (n >= ULMK_BOARD_SPI_MAX)
		return ULMK_TID_INVALID;
	if (g_spi_eps[n] != ULMK_EP_INVALID && g_spi_eps[n] != 0)
		return ULMK_TID_INVALID;

	spi = &g_spi[n];
	spi->hw = &g_spi_hw[n];
	spi->rx_slot = (uint8_t)GDMA_AXI_SLOT_RX(n + 1u);
	spi->tx_slot = (uint8_t)GDMA_AXI_SLOT_TX(n + 1u);
	periph = GDMA_AXI_PERIPH_SPI2 + n;

	/* Root must have created the shared GDMA-AXI server first. */
	if (gdma_axi_channel_open(spi->rx_slot, periph, 0u) != ULMK_OK)
		return ULMK_TID_INVALID;
	if (gdma_axi_channel_open(spi->tx_slot, periph, 0u) != ULMK_OK)
		return ULMK_TID_INVALID;

	spi_pins(spi, 1);
	spi_hw_init(spi);

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_spi_eps[n] = ep;

	attr.name       = "spi";
	attr.entry      = spi_server;
	attr.arg        = (void *)(uintptr_t)n;
	attr.priority   = 2u;
	attr.stack_size = SPI_STACK;
	attr.privilege  = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		g_spi_eps[n] = ULMK_EP_INVALID;
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	board_console_printf("spi%u: gdma-axi\r\n", n + 2u);
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
