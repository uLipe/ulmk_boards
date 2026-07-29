/* SPDX-License-Identifier: MIT */
/*
 * TWAI0 — self-test (no ACK) loopback.
 *
 * Owns the controller from its own server thread; clients reach it only over
 * IPC.  TX arbitration and completion block on the TWAI interrupt via
 * ulmk_notif_wait instead of spinning on the status register.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "can.h"
#include "can_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"

#define TWAI_BASE		ULMK_BOARD_TWAI0_BASE
#define TWAI_MODE		(TWAI_BASE + 0x00u)
#define TWAI_CMD		(TWAI_BASE + 0x04u)
#define TWAI_STATUS		(TWAI_BASE + 0x08u)
#define TWAI_INT		(TWAI_BASE + 0x0cu)	/* read clears */
#define TWAI_INT_ENA		(TWAI_BASE + 0x10u)
#define TWAI_BUS_TIMING_0	(TWAI_BASE + 0x18u)
#define TWAI_BUS_TIMING_1	(TWAI_BASE + 0x1cu)
#define TWAI_RX_ERR		(TWAI_BASE + 0x38u)
#define TWAI_TX_ERR		(TWAI_BASE + 0x3cu)
#define TWAI_DATA(n)		(TWAI_BASE + 0x40u + 4u * (n))

#define MODE_RESET		(1u << 0)
#define MODE_LISTEN		(1u << 1)
#define MODE_SELF_TEST		(1u << 2)
#define MODE_FILTER		(1u << 3)

#define CMD_TX			(1u << 0)
#define CMD_ABORT		(1u << 1)
#define CMD_RELEASE_RX		(1u << 2)
#define CMD_CLR_OVERRUN		(1u << 3)
#define CMD_SELF_RX		(1u << 4)

#define ST_RXBUF		(1u << 0)
#define ST_TXBUF		(1u << 2)
#define ST_TX_COMPLETE		(1u << 3)

/* INTERRUPT / INTERRUPT_ENABLE share the same bit order. */
#define INT_RX			(1u << 0)
#define INT_TX			(1u << 1)

/* Single notif bit: the handler reads INTERRUPT to find out what fired. */
#define CAN_NOTIF_IRQ		0u
/* Generous next to a frame time; only trips when the bus is not answering. */
#define CAN_WAIT_MS		50u

#define TWAI_TX_OUT		80u
#define TWAI_RX_IN		80u

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL2	(HP_CLKRST + 0x1cu)
#define HP_PERI_CLK_CTRL115	(HP_CLKRST + 0x7cu)
#define HP_RST_EN1		(HP_CLKRST + 0xc4u)

#define TWAI0_APB_EN		(1u << 24)
#define TWAI0_CLK_EN		(1u << 25)

struct can_inst {
	uint8_t		index;
	uint8_t		ready;
	uint8_t		irq;
	ulmk_notif_t	notif;
	uint32_t	last_id;
	uint8_t		last[8];
	uint8_t		last_len;
};

ulmk_ep_t g_can_eps[CAN_MAX_INST];
static struct can_inst g_inst[CAN_MAX_INST];

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

/*
 * Self-test loop: the TX pad is fed straight back into the RX signal through
 * the GPIO matrix, so no transceiver is needed.  The pad's input buffer has
 * to be on, otherwise the matrix reads a constant dominant level, the
 * controller never sees the bus go idle and no transmission ever completes.
 * The pull-up leaves the line recessive while the peripheral is not driving,
 * which is how an unterminated CAN bus idles.
 */
static void can_pins(void)
{
	pinmux_cfg_t cfg = {0};

	cfg.pin = ULMK_BOARD_TWAI_TX_GPIO;
	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_UP;
	cfg.alt = PINMUX_ALT_MATRIX;
	cfg.flags = PINMUX_F_PERIPH_OE | PINMUX_F_IE;
	cfg.matrix_out = TWAI_TX_OUT;
	cfg.matrix_in = TWAI_RX_IN;
	(void)pinmux_apply(&cfg);
}

/*
 * Blocks until one of @mask fires.  The peripheral enable is kept off outside
 * this window: the CLIC line is level-driven, so leaving it armed would
 * re-enter the handler until the status register is read.  The wait is
 * bounded because a bus with no listener leaves the controller retrying
 * forever, and a driver server thread must not become unresponsive.
 */
static int twai_wait_int(struct can_inst *c, uint32_t mask)
{
	uint32_t bits = 0u;
	int rc;

	(void)rd(TWAI_INT);
	(void)ulmk_irq_ack(c->irq);
	wr(TWAI_INT_ENA, mask);

	rc = ulmk_notif_wait_timeout(c->notif, 1u << CAN_NOTIF_IRQ, &bits,
				     CAN_WAIT_MS);

	wr(TWAI_INT_ENA, 0u);
	(void)rd(TWAI_INT);
	return rc;
}

static void rx_drain(struct can_inst *c)
{
	uint8_t i;
	uint8_t dlc;
	uint32_t rid;

	if ((rd(TWAI_STATUS) & ST_RXBUF) == 0u)
		return;

	dlc = (uint8_t)(rd(TWAI_DATA(0)) & 0x0Fu);
	rid = ((rd(TWAI_DATA(1)) << 3) | (rd(TWAI_DATA(2)) >> 5)) & 0x7FFu;
	c->last_id = rid;
	c->last_len = dlc > 8u ? 8u : dlc;
	for (i = 0u; i < c->last_len; i++)
		c->last[i] = (uint8_t)rd(TWAI_DATA(3u + i));
	wr(TWAI_CMD, CMD_RELEASE_RX);
}

static int hw_send(struct can_inst *c, uint32_t id, const uint8_t *data,
		   uint8_t len)
{
	uint8_t i;

	if (!c->ready)
		return ULMK_EINVAL;
	if (len > 8u)
		len = 8u;

	if ((rd(TWAI_STATUS) & ST_TXBUF) == 0u) {
		if (twai_wait_int(c, INT_TX) != ULMK_OK)
			return ULMK_ETIMEOUT;
	}

	/*
	 * SJA1000 frame layout: D0 = FF/RTR/DLC, D1-D2 = 11-bit ID,
	 * D3.. = payload.
	 */
	wr(TWAI_DATA(0), (uint32_t)(len & 0x0Fu));
	wr(TWAI_DATA(1), (id >> 3) & 0xFFu);
	wr(TWAI_DATA(2), (id << 5) & 0xE0u);
	for (i = 0u; i < len; i++)
		wr(TWAI_DATA(3u + i), data ? data[i] : 0u);

	/*
	 * Self-reception rather than a plain transmit request: in self-test
	 * mode a plain request puts the frame on the wire but never files it
	 * in the RXFIFO, and it would also wait for an ACK that no other node
	 * is there to give.
	 */
	wr(TWAI_CMD, CMD_SELF_RX);

	/*
	 * Wait on reception, not on transmit-complete: that status bit still
	 * reports the previous frame and is already set on a fresh controller,
	 * so testing it here would fall straight through and drain an empty
	 * RXFIFO.
	 */
	if ((rd(TWAI_STATUS) & ST_RXBUF) == 0u) {
		if (twai_wait_int(c, INT_RX) != ULMK_OK) {
			wr(TWAI_CMD, CMD_ABORT);
			return ULMK_ETIMEOUT;
		}
	}

	rx_drain(c);
	return ULMK_OK;
}

static void can_server(void *arg)
{
	struct can_inst *c = (struct can_inst *)arg;
	ulmk_ep_t ep = g_can_eps[c->index];
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;
	uint8_t buf[8];
	uint8_t i;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;

		switch (msg.label) {
		case CAN_MSG_SEND:
			for (i = 0u; i < 4u; i++) {
				buf[i] = (uint8_t)(msg.words[2] >> (8u * i));
				buf[4u + i] =
					(uint8_t)(msg.words[3] >> (8u * i));
			}
			reply.words[0] = (uint32_t)hw_send(
				c, msg.words[0], buf, (uint8_t)msg.words[1]);
			break;
		case CAN_MSG_RECV:
			reply.words[0] = (uint32_t)ULMK_OK;
			reply.words[1] = c->last_id;
			reply.words[2] = c->last_len;
			reply.words[3] = 0u;
			reply.words[4] = 0u;
			for (i = 0u; i < c->last_len && i < 4u; i++)
				reply.words[3] |=
					(uint32_t)c->last[i] << (8u * i);
			for (i = 4u; i < c->last_len; i++)
				reply.words[4] |= (uint32_t)c->last[i] <<
						  (8u * (i - 4u));
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t can_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	struct can_inst *c;
	uint32_t clic_reg;
	ulmk_tid_t tid;
	uint32_t v;
	uint32_t i;

	if (n >= CAN_MAX_INST)
		return ULMK_TID_INVALID;

	c = &g_inst[n];
	if (c->ready)
		return ULMK_TID_INVALID;
	c->index = n;
	c->irq = (uint8_t)ULMK_BOARD_IRQ_TWAI0;
	c->last_id = 0u;
	c->last_len = 0u;

	g_can_eps[n] = ulmk_ep_create();
	if (g_can_eps[n] == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	c->notif = ulmk_notif_create();
	if (c->notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;

	can_pins();

	v = rd(HP_SOC_CLK_CTRL2);
	wr(HP_SOC_CLK_CTRL2, v | TWAI0_APB_EN);

	v = rd(HP_PERI_CLK_CTRL115);
	wr(HP_PERI_CLK_CTRL115, v | TWAI0_CLK_EN);

	v = rd(HP_RST_EN1);
	wr(HP_RST_EN1, v | (1u << 26)); /* rst_en_twai0 */
	wr(HP_RST_EN1, v & ~(1u << 26));

	wr(TWAI_MODE, rd(TWAI_MODE) | MODE_RESET);
	wr(TWAI_MODE, MODE_RESET | MODE_SELF_TEST);
	wr(TWAI_INT_ENA, 0u);
	wr(TWAI_BUS_TIMING_0, 0x04u);
	wr(TWAI_BUS_TIMING_1, 0x1cu);
	/*
	 * Accept-all filter.  While in reset mode the data registers alias the
	 * acceptance code and mask, and both come up zero — a mask of zero
	 * demands an exact match, so only identifier 0x000 would ever reach
	 * the RXFIFO and a self-received frame gets dropped before arrival.
	 */
	for (i = 0u; i < 4u; i++) {
		wr(TWAI_DATA(i), 0u);
		wr(TWAI_DATA(4u + i), 0xFFu);
	}
	wr(TWAI_RX_ERR, 0u);
	wr(TWAI_TX_ERR, 0u);
	wr(TWAI_MODE, MODE_SELF_TEST);
	(void)rd(TWAI_INT);

	/* The kernel routes the interrupt matrix as part of the bind below. */
	clic_reg = ULMK_BOARD_CLIC_BASE + 0x1000u +
		   (uint32_t)ULMK_BOARD_CLIC_IRQ_TWAI0 * 4u;
	if (ulmk_irq_bind_hw(c->irq, c->notif, CAN_NOTIF_IRQ,
			     clic_reg) != ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(c->irq) != ULMK_OK)
		return ULMK_TID_INVALID;

	c->ready = 1u;

	attr.name = "twai0";
	attr.entry = can_server;
	attr.arg = c;
	attr.priority = 2u;
	attr.stack_size = 2048u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH | ULMK_CAP_IRQ);
	return tid;
}
