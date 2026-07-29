/* SPDX-License-Identifier: MIT */
/*
 * UART server — one IPC server thread per controller (UART0..UART4).
 *
 * TX blocks on the TXFIFO-empty IRQ via ulmk_notif_wait rather than
 * polling the FIFO level.  Passing pins == NULL and baud == 0 adopts the
 * pin mux and divider the bootloader left in place, which is what the
 * console instance needs so output does not change link mid-boot.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "uart.h"
#include "uart_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"

#define UART0_BASE	ULMK_BOARD_UART0_BASE
#define UART_STRIDE	0x1000u

#define UART_FIFO	0x00u
#define UART_INT_RAW	0x04u
#define UART_INT_ST	0x08u
#define UART_INT_ENA	0x0cu
#define UART_INT_CLR	0x10u
#define UART_CLKDIV	0x14u
#define UART_STATUS	0x1cu
#define UART_CONF0	0x20u
#define UART_CONF1	0x24u
#define UART_CLK_CONF	0x88u
#define UART_REG_UPD	0x98u

#define INT_TXFIFO_EMPTY	(1u << 1)
#define TXFIFO_CNT_SHIFT	16
#define TXFIFO_CNT_MASK		0xFFu
#define TXFIFO_DEPTH		128u
#define TX_EMPTY_THRHD		32u
#define CONF1_TX_THRHD_SHIFT	8
#define CONF1_TX_THRHD_MASK	0xFFu
#define RXFIFO_CNT_MASK		0xFFu

#define CLK_CONF_TX_SCLK_EN	(1u << 24)
#define CLK_CONF_RX_SCLK_EN	(1u << 25)

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL2	(HP_CLKRST + 0x1cu)

/* gpio_sig_map: UARTn RXD/TXD pad signals are 10 + 3*n. */
#define UART_SIG_IDX(n)		(10u + 3u * (uint32_t)(n))

#define UART_NOTIF_TX		0u

struct uart_inst {
	uint32_t base;
	ulmk_notif_t notif;
	uint8_t irq;
	uint8_t index;
	uint8_t ready;
};

ulmk_ep_t g_uart_eps[UART_MAX_INST];
static struct uart_inst g_inst[UART_MAX_INST];

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

/*
 * CLKDIV/CONF0/CONF1 are the *_SYNC flavour on this SoC: writes stage in
 * shadow registers and only reach the core once REG_UPDATE is pulsed.
 */
static void reg_commit(const struct uart_inst *u)
{
	wr(u->base + UART_REG_UPD, 1u);
	while (rd(u->base + UART_REG_UPD) & 1u)
		;
}

static inline uint32_t tx_used(const struct uart_inst *u)
{
	return (rd(u->base + UART_STATUS) >> TXFIFO_CNT_SHIFT) &
	       TXFIFO_CNT_MASK;
}

static void uart_hw_init(struct uart_inst *u, uint32_t baud)
{
	uint32_t div;
	uint32_t v;

	v = rd(HP_SOC_CLK_CTRL2);
	wr(HP_SOC_CLK_CTRL2, v | (1u << (12u + u->index)));

	wr(u->base + UART_CLK_CONF,
	   CLK_CONF_TX_SCLK_EN | CLK_CONF_RX_SCLK_EN);

	div = (40000000u << 4) / baud; /* XTAL * 16 / baud, fractional form */
	wr(u->base + UART_CLKDIV, div);
	wr(u->base + UART_CONF0, (3u << 0)); /* 8N1 */
	reg_commit(u);
}

static void uart_pins(uint8_t n, uint8_t tx, uint8_t rx)
{
	pinmux_cfg_t cfg = {0};

	cfg.pin = tx;
	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_PERIPH_OE;
	cfg.matrix_out = UART_SIG_IDX(n);
	(void)pinmux_apply(&cfg);

	cfg.pin = rx;
	cfg.dir = PINMUX_DIR_IN;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_IE;
	cfg.matrix_out = 0u;
	cfg.matrix_in = UART_SIG_IDX(n);
	(void)pinmux_apply(&cfg);
}

static void tx_wait_space(struct uart_inst *u)
{
	uint32_t bits;

	if (tx_used(u) < TXFIFO_DEPTH)
		return;

	wr(u->base + UART_INT_CLR, INT_TXFIFO_EMPTY);
	(void)ulmk_irq_ack(u->irq);
	wr(u->base + UART_INT_ENA, INT_TXFIFO_EMPTY);

	bits = 0u;
	(void)ulmk_notif_wait(u->notif, 1u << UART_NOTIF_TX, &bits);

	wr(u->base + UART_INT_ENA, 0u);
	wr(u->base + UART_INT_CLR, INT_TXFIFO_EMPTY);
}

static int hw_tx_byte(struct uart_inst *u, uint8_t b)
{
	if (!u->ready)
		return ULMK_ENOTSUP;
	tx_wait_space(u);
	wr(u->base + UART_FIFO, b);
	return ULMK_OK;
}

static void uart_server(void *arg)
{
	struct uart_inst *u = (struct uart_inst *)arg;
	ulmk_ep_t ep = g_uart_eps[u->index];
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		switch (msg.label) {
		case UART_MSG_TXB:
			reply.words[0] = (uint32_t)hw_tx_byte(
				u, (uint8_t)msg.words[0]);
			break;
		case UART_MSG_TXBUF: {
			const uint8_t *p = (const uint8_t *)(uintptr_t)
				msg.words[0];
			size_t n = (size_t)msg.words[1];
			size_t i;
			int rc = ULMK_OK;

			for (i = 0u; i < n; i++) {
				rc = hw_tx_byte(u, p[i]);
				if (rc != ULMK_OK)
					break;
			}
			reply.words[0] = (uint32_t)rc;
			break;
		}
		case UART_MSG_RXB:
			/* Non-blocking peek; empty → ETIMEOUT. */
			if ((rd(u->base + UART_STATUS) & RXFIFO_CNT_MASK) == 0u) {
				reply.words[0] = (uint32_t)ULMK_ETIMEOUT;
			} else {
				reply.words[0] = (uint32_t)ULMK_OK;
				reply.words[1] = rd(u->base + UART_FIFO) & 0xFFu;
			}
			break;
		case UART_MSG_BAUD:
			if (msg.words[0] != 0u) {
				uart_hw_init(u, msg.words[0]);
				reply.words[0] = (uint32_t)ULMK_OK;
			}
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t uart_init(uint8_t n, const uart_pins_t *pins, uint32_t baud,
		     uint32_t pclk_hz)
{
	static const char *const names[UART_MAX_INST] = {
		"uart0", "uart1", "uart2", "uart3", "uart4"
	};
	ulmk_thread_attr_t attr = {0};
	struct uart_inst *u;
	uint32_t clic_reg;
	ulmk_tid_t tid;
	uint32_t v;

	(void)pclk_hz;
	if (n >= UART_MAX_INST)
		return ULMK_TID_INVALID;

	u = &g_inst[n];
	if (u->ready)
		return ULMK_TID_INVALID;
	u->index = n;
	u->base = UART0_BASE + (uint32_t)n * UART_STRIDE;
	u->irq = (uint8_t)(ULMK_BOARD_IRQ_UART_BASE + n);

	g_uart_eps[n] = ulmk_ep_create();
	if (g_uart_eps[n] == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	u->notif = ulmk_notif_create();
	if (u->notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;

	if (pins)
		uart_pins(n, pins->tx_gpio, pins->rx_gpio);
	if (baud)
		uart_hw_init(u, baud);

	wr(u->base + UART_INT_ENA, 0u);
	wr(u->base + UART_INT_CLR, 0xFFFFFFFFu);
	v = rd(u->base + UART_CONF1);
	v &= ~(CONF1_TX_THRHD_MASK << CONF1_TX_THRHD_SHIFT);
	v |= TX_EMPTY_THRHD << CONF1_TX_THRHD_SHIFT;
	wr(u->base + UART_CONF1, v);
	reg_commit(u);

	/* INTMTX routing is done once in M-mode board init, not from here. */
	clic_reg = ULMK_BOARD_CLIC_BASE + 0x1000u +
		   (ULMK_BOARD_CLIC_IRQ_UART_BASE + (uint32_t)n) * 4u;
	if (ulmk_irq_bind_hw(u->irq, u->notif, UART_NOTIF_TX,
			     clic_reg) != ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(u->irq) != ULMK_OK)
		return ULMK_TID_INVALID;

	u->ready = 1u;

	attr.name = names[n];
	attr.entry = uart_server;
	attr.arg = u;
	attr.priority = 2u;
	attr.stack_size = 2048u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH | ULMK_CAP_IRQ);
	return tid;
}
