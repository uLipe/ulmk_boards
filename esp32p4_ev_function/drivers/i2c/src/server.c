/* SPDX-License-Identifier: MIT */
/*
 * I2C0 HW master — GPIO8=SCL, GPIO7=SDA (Function EV touch bus).
 * Completion via CLIC IRQ + ulmk_notif_wait (no bitbang, no busy-wait).
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "i2c_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"

#define I2C_STACK		2048u
#define I2C_NOTIF_EV		0u

#define I2C_SCL_GPIO		8u
#define I2C_SDA_GPIO		7u
#define I2C0_SCL_SIG		68u
#define I2C0_SDA_SIG		69u

#define I2C_BASE		ULMK_BOARD_I2C0_BASE
#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL2	(HP_CLKRST + 0x1cu)
#define HP_PERI_CLK_CTRL10	(HP_CLKRST + 0x40u)
#define HP_RST_EN1		(HP_CLKRST + 0xc4u)
#define I2C0_APB_EN		(1u << 12)
#define I2C0_RST_EN		(1u << 22)
#define I2C0_CLK_EN		(1u << 1)
/* src_sel[0], div_num[9:2], div_numerator[17:10], div_denominator[25:18] */
#define I2C0_CLK_CFG_MASK	0x03FFFFFDu

#define I2C_SCL_LOW		(I2C_BASE + 0x00u)
#define I2C_CTR			(I2C_BASE + 0x04u)
#define I2C_SR			(I2C_BASE + 0x08u)
#define I2C_TO			(I2C_BASE + 0x0cu)
#define I2C_FIFO_CONF		(I2C_BASE + 0x18u)
#define I2C_DATA		(I2C_BASE + 0x1cu)
#define I2C_INT_RAW		(I2C_BASE + 0x20u)
#define I2C_INT_CLR		(I2C_BASE + 0x24u)
#define I2C_INT_ENA		(I2C_BASE + 0x28u)
#define I2C_SDA_HOLD		(I2C_BASE + 0x30u)
#define I2C_SDA_SAMPLE		(I2C_BASE + 0x34u)
#define I2C_SCL_HIGH		(I2C_BASE + 0x38u)
#define I2C_SCL_START_HOLD	(I2C_BASE + 0x40u)
#define I2C_SCL_RSTART_SETUP	(I2C_BASE + 0x44u)
#define I2C_SCL_STOP_HOLD	(I2C_BASE + 0x48u)
#define I2C_SCL_STOP_SETUP	(I2C_BASE + 0x4cu)
#define I2C_FILTER_CFG		(I2C_BASE + 0x50u)
#define I2C_COMD(n)		(I2C_BASE + 0x58u + (n) * 4u)

#define I2C_CTR_MS_MODE		(1u << 4)
#define I2C_CTR_TRANS_START	(1u << 5)
#define I2C_CTR_FSM_RST		(1u << 10)
#define I2C_CTR_CONF_UPGATE	(1u << 11)

#define I2C_FIFO_RX_RST		(1u << 12)
#define I2C_FIFO_TX_RST		(1u << 13)

#define I2C_SR_BUS_BUSY		(1u << 4)
#define I2C_SR_SCL_STATE	(7u << 28)

#define I2C_INT_END_DETECT	(1u << 3)
#define I2C_INT_ARBIT_LOST	(1u << 5)
#define I2C_INT_TRANS_COMPLETE	(1u << 7)
#define I2C_INT_TIMEOUT		(1u << 8)
#define I2C_INT_NACK		(1u << 10)
#define I2C_MASTER_EV_MASK	(I2C_INT_END_DETECT | I2C_INT_ARBIT_LOST | \
				 I2C_INT_TRANS_COMPLETE | I2C_INT_TIMEOUT | \
				 I2C_INT_NACK)

/* Opcodes are chip-specific: RSTART is 6 here, not the legacy 0. */
#define I2C_CMD_WRITE		1u
#define I2C_CMD_STOP		2u
#define I2C_CMD_READ		3u
#define I2C_CMD_RSTART		6u

#define I2C_CMD_ACK_VALUE	(1u << 10)

ulmk_ep_t g_i2c_ep;
static ulmk_notif_t g_i2c_notif;
static ulmk_tid_t g_i2c_tid = ULMK_TID_INVALID;
static uint8_t g_hw_ok;

#include "board_console.h"

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static uint32_t make_cmd(uint8_t op, uint8_t nbytes, int ack_en)
{
	uint32_t c;

	c = (uint32_t)nbytes;
	if (ack_en)
		c |= (1u << 8);
	c |= ((uint32_t)op << 11);
	return c;
}

static void i2c_pins(void)
{
	pinmux_cfg_t cfg = {0};
	volatile uint32_t *out_w1ts =
		(volatile uint32_t *)(ULMK_BOARD_GPIO_BASE + 0x08u);

	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_UP;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_OPENDRAIN | PINMUX_F_IE | PINMUX_F_PERIPH_OE;

	cfg.pin = I2C_SCL_GPIO;
	cfg.matrix_out = I2C0_SCL_SIG;
	cfg.matrix_in = I2C0_SCL_SIG;
	(void)pinmux_apply(&cfg);
	*out_w1ts = 1u << I2C_SCL_GPIO;

	cfg.pin = I2C_SDA_GPIO;
	cfg.matrix_out = I2C0_SDA_SIG;
	cfg.matrix_in = I2C0_SDA_SIG;
	(void)pinmux_apply(&cfg);
	*out_w1ts = 1u << I2C_SDA_GPIO;
}

/*
 * 100 kHz @ XTAL 40 MHz (i2c_ll_master_cal_bus_clk).
 * clkm_div=1, half_cycle=200.
 */
static void i2c_hw_init(void)
{
	uint32_t v;
	uint32_t half = 200u;
	uint32_t wait_h;
	uint32_t scl_h;

	v = rd(HP_SOC_CLK_CTRL2);
	wr(HP_SOC_CLK_CTRL2, v | I2C0_APB_EN);

	v = rd(HP_RST_EN1);
	wr(HP_RST_EN1, v | I2C0_RST_EN);
	wr(HP_RST_EN1, v & ~I2C0_RST_EN);

	/*
	 * XTAL src (bit0=0), div_num=0 → /1.  The fractional divider
	 * (numerator/denominator) must be cleared too: whatever the
	 * bootloader left there keeps skewing the module clock.
	 */
	v = rd(HP_PERI_CLK_CTRL10);
	v &= ~I2C0_CLK_CFG_MASK;
	v |= I2C0_CLK_EN;
	wr(HP_PERI_CLK_CTRL10, v);

	i2c_pins();

	wait_h = half / 2u - 2u;
	scl_h = half - wait_h;
	wr(I2C_SCL_LOW, half - 1u);
	wr(I2C_SCL_HIGH, scl_h | (wait_h << 9));
	wr(I2C_SDA_HOLD, half / 4u - 1u);
	wr(I2C_SDA_SAMPLE, half / 2u - 1u);
	wr(I2C_SCL_START_HOLD, half - 1u);
	wr(I2C_SCL_RSTART_SETUP, half - 1u);
	wr(I2C_SCL_STOP_HOLD, half - 1u);
	wr(I2C_SCL_STOP_SETUP, half - 1u);
	/* SCL stuck timeout: 2^20 module cycles ≈ 26 ms @40 MHz. */
	wr(I2C_TO, (20u << 0) | (1u << 5));
	wr(I2C_FILTER_CFG, (7u << 0) | (7u << 4) | (1u << 8) | (1u << 9));

	/* master, MSB first, open-drain pads (force_out=0), no arbitration */
	wr(I2C_CTR, I2C_CTR_MS_MODE | I2C_CTR_CONF_UPGATE);

	/* FIFO reset pulse */
	v = rd(I2C_FIFO_CONF);
	wr(I2C_FIFO_CONF, v | I2C_FIFO_RX_RST | I2C_FIFO_TX_RST);
	wr(I2C_FIFO_CONF, v & ~(I2C_FIFO_RX_RST | I2C_FIFO_TX_RST));

	wr(I2C_INT_CLR, 0x3FFFu);
	wr(I2C_INT_ENA, I2C_MASTER_EV_MASK);
	g_hw_ok = 1u;
	board_console_printf("ulmk: i2c0 hw ready\n");
}

static int wait_ev(void)
{
	uint32_t bits = 0u;
	int ret;

	ulmk_irq_ack(ULMK_BOARD_IRQ_I2C0);
	ret = ulmk_notif_wait_timeout(g_i2c_notif, 1u << I2C_NOTIF_EV,
				      &bits, 50u);
	ulmk_irq_ack(ULMK_BOARD_IRQ_I2C0);
	/*
	 * If CLIC routing missed the edge, still succeed when the
	 * controller latched a completion/error in INT_RAW.
	 */
	if (ret != ULMK_OK && (rd(I2C_INT_RAW) & I2C_MASTER_EV_MASK))
		return ULMK_OK;
	return ret;
}

static int finish_xfer(void)
{
	uint32_t raw;
	int ret;

	ret = wait_ev();
	raw = rd(I2C_INT_RAW);
	wr(I2C_INT_CLR, 0x3FFFu);

	if (ret != ULMK_OK)
		return ret;
	if (raw & I2C_INT_NACK)
		return ULMK_ESRCH;
	if (raw & (I2C_INT_TIMEOUT | I2C_INT_ARBIT_LOST))
		return ULMK_EINVAL;
	if (raw & (I2C_INT_TRANS_COMPLETE | I2C_INT_END_DETECT))
		return ULMK_OK;
	return ULMK_EINVAL;
}

/*
 * Every transfer starts with an RSTART command: the controller only drives
 * the START condition when the command list says so, and without it no slave
 * ever sees its address.
 */
static void xfer_begin(void)
{
	uint32_t v;

	/*
	 * A transfer raises several enabled sources (NACK + TRANS_COMPLETE
	 * on every probe miss), so the notification is still signalled once
	 * the previous wait returned.  Drain it here, otherwise the next
	 * wait returns immediately and finish_xfer() clears I2C_INT while
	 * the controller is mid-byte, leaving SCL held low forever.
	 */
	(void)ulmk_notif_poll(g_i2c_notif, 1u << I2C_NOTIF_EV);

	wr(I2C_INT_CLR, 0x3FFFu);
	if ((rd(I2C_SR) & I2C_SR_BUS_BUSY) != 0u)
		wr(I2C_CTR, rd(I2C_CTR) | I2C_CTR_FSM_RST);
	v = rd(I2C_FIFO_CONF);
	wr(I2C_FIFO_CONF, v | I2C_FIFO_RX_RST | I2C_FIFO_TX_RST);
	wr(I2C_FIFO_CONF, v & ~(I2C_FIFO_RX_RST | I2C_FIFO_TX_RST));
}

static void xfer_start(void)
{
	wr(I2C_CTR, rd(I2C_CTR) | I2C_CTR_CONF_UPGATE);
	wr(I2C_CTR, rd(I2C_CTR) | I2C_CTR_TRANS_START);
}

static int i2c_do_probe(uint8_t addr7)
{
	if (!g_hw_ok)
		return ULMK_ENOTSUP;

	xfer_begin();
	wr(I2C_DATA, (uint32_t)(addr7 << 1));
	wr(I2C_COMD(0), make_cmd(I2C_CMD_RSTART, 0u, 0));
	wr(I2C_COMD(1), make_cmd(I2C_CMD_WRITE, 1u, 1));
	wr(I2C_COMD(2), make_cmd(I2C_CMD_STOP, 0u, 0));
	xfer_start();

	return finish_xfer();
}

static int i2c_do_write(uint8_t addr7, const uint8_t *data, size_t len)
{
	uint32_t i;

	if (!g_hw_ok)
		return ULMK_ENOTSUP;
	if (len + 1u > I2C_XFER_MAX)
		return ULMK_EINVAL;

	xfer_begin();
	wr(I2C_DATA, (uint32_t)(addr7 << 1));
	for (i = 0u; i < len; i++)
		wr(I2C_DATA, data ? data[i] : 0u);

	wr(I2C_COMD(0), make_cmd(I2C_CMD_RSTART, 0u, 0));
	wr(I2C_COMD(1), make_cmd(I2C_CMD_WRITE, (uint8_t)(len + 1u), 1));
	wr(I2C_COMD(2), make_cmd(I2C_CMD_STOP, 0u, 0));
	xfer_start();

	return finish_xfer();
}

static int i2c_do_read(uint8_t addr7, uint8_t *data, size_t len)
{
	uint32_t i;
	uint32_t n;

	if (!g_hw_ok || !data || len == 0u || len > I2C_XFER_MAX)
		return !g_hw_ok ? ULMK_ENOTSUP : ULMK_EINVAL;

	xfer_begin();
	wr(I2C_DATA, (uint32_t)((addr7 << 1) | 1u));
	wr(I2C_COMD(0), make_cmd(I2C_CMD_RSTART, 0u, 0));
	wr(I2C_COMD(1), make_cmd(I2C_CMD_WRITE, 1u, 1));

	/* All but the last byte are ACKed; the last one must be NACKed. */
	n = 2u;
	if (len > 1u)
		wr(I2C_COMD(n++), make_cmd(I2C_CMD_READ,
					   (uint8_t)(len - 1u), 0));
	wr(I2C_COMD(n++), make_cmd(I2C_CMD_READ, 1u, 0) | I2C_CMD_ACK_VALUE);
	wr(I2C_COMD(n), make_cmd(I2C_CMD_STOP, 0u, 0));
	xfer_start();

	if (finish_xfer() != ULMK_OK)
		return ULMK_EINVAL;
	for (i = 0u; i < len; i++)
		data[i] = (uint8_t)(rd(I2C_DATA) & 0xFFu);
	return ULMK_OK;
}

static void i2c_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t buf[I2C_XFER_MAX];
	size_t len;
	uint32_t i;

	(void)arg;
	board_console_printf("ulmk: i2c0 server\n");
	i2c_hw_init();

	for (;;) {
		if (ulmk_ep_recv(g_i2c_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;

		switch (msg.label) {
		case I2C_MSG_PROBE:
			reply.words[0] = (uint32_t)i2c_do_probe(
				(uint8_t)msg.words[0]);
			break;
		case I2C_MSG_WRITE:
			len = (size_t)msg.words[1];
			if (len > I2C_XFER_MAX)
				len = I2C_XFER_MAX;
			for (i = 0u; i < len; i++)
				buf[i] = (uint8_t)((msg.words[2u + i / 4u] >>
						    ((i % 4u) * 8u)) & 0xFFu);
			reply.words[0] = (uint32_t)i2c_do_write(
				(uint8_t)msg.words[0], buf, len);
			break;
		case I2C_MSG_READ:
			len = (size_t)msg.words[1];
			if (len > 12u)
				len = 12u;
			reply.words[0] = (uint32_t)i2c_do_read(
				(uint8_t)msg.words[0], buf, len);
			if ((int)(int32_t)reply.words[0] == ULMK_OK) {
				for (i = 0u; i < len; i++)
					reply.words[1u + i / 4u] |=
						((uint32_t)buf[i] <<
						 ((i % 4u) * 8u));
			}
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t i2c_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	uintptr_t clic_reg;

	if (n != 0u)
		return ULMK_TID_INVALID;
	if (g_i2c_ep != ULMK_EP_INVALID && g_i2c_ep != 0)
		return g_i2c_tid;

	g_i2c_ep = ulmk_ep_create();
	g_i2c_notif = ulmk_notif_create();
	if (g_i2c_ep == ULMK_EP_INVALID ||
	    g_i2c_notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;

	/* INTMTX routing is done once in M-mode board init, not from here. */
	clic_reg = ULMK_BOARD_CLIC_BASE + 0x1000u +
		   ULMK_BOARD_CLIC_IRQ_I2C0 * 4u;
	if (ulmk_irq_bind_hw(ULMK_BOARD_IRQ_I2C0, g_i2c_notif, I2C_NOTIF_EV,
			     clic_reg) != ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_I2C0) != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name = "i2c0";
	attr.entry = i2c_server;
	attr.priority = 2u;
	attr.stack_size = I2C_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	g_i2c_tid = tid;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
