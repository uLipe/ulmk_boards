/* SPDX-License-Identifier: MIT */
/*
 * can server — MultiCAN, IRQ + notif (no busy-wait on RX).
 *
 * Lite Kit pins (always muxed for later real-bus use):
 *   P20.8 TX / P20.7 RX → TLE9251, P20.6 #NEN driven low.
 *
 * Loopback: Node0 TX + Node1 RX, both LBM=1 on the internal MultiCAN
 * bus (Infineon MULTICAN_1 pattern).  A single node in LBM does not
 * self-ACK (LEC=3 / TXRQ stuck).
 * Without loopback: both MOs on Node0, LBM=0 (external bus + TLE9251).
 *
 * Clock (fCAN = fSPB) is programmed in board_init under EndInit.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "can_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"
#include "Port/Std/IfxPort.h"
#include "IfxCan_reg.h"
#include "Multican/Std/IfxMultican.h"

#define CAN_STACK		2048u
#define CAN_MAP_SIZE		0x2000u
#define CAN_NOTIF_RX		0u
#define CAN_SRPN		ULMK_BOARD_IRQ_CAN0
#define CAN_SRC			ULMK_BOARD_SRC_CAN0_INT0
#define CAN_MO_TX		0u
#define CAN_MO_RX		1u
#define CAN_BTR_500K		0x3EC9u

struct can_board_cfg {
	uint8_t tx_port;
	uint8_t tx_pin;
	uint8_t tx_alt;
	uint8_t rx_port;
	uint8_t rx_pin;
	uint8_t rx_alti;
	uint8_t nen_port;
	uint8_t nen_pin;
	uint8_t loopback;
};

typedef struct {
	uint8_t             n;
	struct can_board_cfg cfg;
	uint32_t            bitrate;
	ulmk_ep_t           ep;
	ulmk_notif_t        irq_notif;
} can_args_t;

ulmk_ep_t g_can_eps[CAN_MAX];
static can_args_t g_args[CAN_MAX] __attribute__((section(".user_bss")));
static Ifx_CAN *g_mcan __attribute__((section(".user_bss")));
static Ifx_CAN_MO *g_mo_tx __attribute__((section(".user_bss")));
static Ifx_CAN_MO *g_mo_rx __attribute__((section(".user_bss")));

static void mo_clr(Ifx_CAN_MO *mo, IfxMultican_MsgObjStatusFlag flag)
{
	mo->CTR.U = 1u << (uint32_t)flag;
}

static void mo_set(Ifx_CAN_MO *mo, IfxMultican_MsgObjStatusFlag flag)
{
	mo->CTR.U = 1u << ((uint32_t)flag + 16u);
}

static int panel_cmd(Ifx_CAN *mcan, uint32_t cmd, uint32_t arg2, uint32_t arg1)
{
	Ifx_CAN_PANCTR panctr;
	uint32_t spin = 0u;

	panctr.U = 0u;
	panctr.B.PANAR1 = arg1;
	panctr.B.PANAR2 = arg2;
	panctr.B.PANCMD = cmd;
	mcan->PANCTR.U = panctr.U;
	while (mcan->PANCTR.B.BUSY != 0u) {
		spin++;
		if (spin > 1000000u)
			return ULMK_ETIMEOUT;
	}
	return ULMK_OK;
}

static void pin_nen_low(uint8_t port, uint8_t pin)
{
	pinmux_cfg_t cfg;
	Ifx_P *p;

	if (port == 0xFFu)
		return;
	cfg.port  = port;
	cfg.pin   = pin;
	cfg.dir   = PINMUX_DIR_OUT;
	cfg.pull  = PINMUX_PULL_NONE;
	cfg.alt   = PINMUX_ALT_GPIO;
	cfg.flags = 0u;
	(void)pinmux_apply(&cfg);
	p = pinmux_port(port);
	if (p)
		p->OMR.U = (1u << (16u + (uint32_t)pin));
}

static void pinmux_can(const struct can_board_cfg *cfg)
{
	pinmux_cfg_t pm;

	pm.port  = cfg->tx_port;
	pm.pin   = cfg->tx_pin;
	pm.dir   = PINMUX_DIR_OUT;
	pm.pull  = PINMUX_PULL_NONE;
	pm.alt   = cfg->tx_alt;
	pm.flags = 0u;
	(void)pinmux_apply(&pm);

	pm.port  = cfg->rx_port;
	pm.pin   = cfg->rx_pin;
	pm.dir   = PINMUX_DIR_IN;
	pm.pull  = PINMUX_PULL_UP;
	pm.alt   = PINMUX_ALT_GPIO;
	(void)pinmux_apply(&pm);

	pin_nen_low(cfg->nen_port, cfg->nen_pin);
}

static uint32_t bitrate_to_btr(uint32_t bitrate)
{
	if (bitrate == 250000u)
		return 0x3ED3u;
	if (bitrate == 125000u)
		return 0x3EE7u;
	return CAN_BTR_500K;
}

/*
 * Leave INIT+CCE set until message objects are allocated.  Starting the
 * node before MO setup is what produced ACK errors / lost NEWDAT on HIL.
 */
static void node_prepare(Ifx_CAN_N *node, uint32_t btr, int loopback,
			 uint8_t rxsel)
{
	node->CR.U = 0x00000041u; /* INIT=1, CCE=1 */
	node->IPR.U = 0u;
	node->ECNT.U = 0x00600000u;
	node->PCR.U = ((loopback != 0) ? (1u << 8) : 0u) | (rxsel & 0x7u);
	node->BTR.U = btr;
}

static void node_start(Ifx_CAN_N *node)
{
	node->CR.B.CCE = 0u;
	node->CR.B.INIT = 0u;
}

static int mo_init_tx(Ifx_CAN_MO *mo, uint8_t list, uint32_t id)
{
	mo->CTR.U = 0x0000FFFFu;
	if (panel_cmd(g_mcan, 0x2u, list, CAN_MO_TX) != ULMK_OK)
		return ULMK_ETIMEOUT;

	mo->FCR.U = (8u << 24); /* DLC=8, MMC=0 */
	mo->FGPR.U = 0u;
	mo->IPR.U = (uint32_t)CAN_MO_TX << 8;
	mo->AMR.U = 0x3FFFFFFFu;
	mo->DATAL.U = 0u;
	mo->DATAH.U = 0u;
	/* PRI=ListOrder (01), standard ID in [28:18] */
	mo->AR.U = (1u << 30) | ((id & 0x7FFu) << 18);

	/* TXEN1|TXEN0|DIR|MSGVAL — match UM / community bring-up */
	mo->CTR.U = (1u << 27) | (1u << 26) | (1u << 25) | (1u << 21);
	return ULMK_OK;
}

static int mo_init_rx(Ifx_CAN_MO *mo, uint8_t list, uint32_t id)
{
	mo->CTR.U = 0x0000FFFFu;
	if (panel_cmd(g_mcan, 0x2u, list, CAN_MO_RX) != ULMK_OK)
		return ULMK_ETIMEOUT;

	mo->FCR.U = (1u << 16) | (8u << 24); /* RXIE + DLC=8 */
	mo->FGPR.U = 0u;
	mo->IPR.U = (uint32_t)CAN_MO_RX << 8;
	mo->AMR.U = 0x3FFFFFFFu;
	mo->DATAL.U = 0u;
	mo->DATAH.U = 0u;
	mo->AR.U = (1u << 30) | ((id & 0x7FFu) << 18);

	/* RXEN|MSGVAL */
	mo->CTR.U = (1u << 23) | (1u << 21);
	return ULMK_OK;
}

static int hw_start(const struct can_board_cfg *cfg, uint32_t bitrate)
{
	Ifx_CAN_N *node_tx;
	Ifx_CAN_N *node_rx;
	uint32_t btr;
	uint32_t i;
	uint8_t list_tx;
	uint8_t list_rx;
	int rc;

	pinmux_can(cfg);

	if (g_mcan->MCR.B.CLKSEL == 0u) {
		g_mcan->MCR.B.CLKSEL = 0u;
		g_mcan->MCR.B.CLKSEL = 1u;
		g_mcan->FDR.U = (1u << 14) | 1023u;
	}

	IfxMultican_waitListReady(g_mcan);
	for (i = 0u; i < 8u; i++)
		IfxMultican_clearPendingMessageNotification(g_mcan, (uint16)i);
	IfxMultican_clearMessagePendingSeletor(g_mcan);
	IfxMultican_setMessageIndexMask(g_mcan, 0xFFFFFFFFu);

	btr = bitrate_to_btr(bitrate ? bitrate : 500000u);
	node_tx = IfxMultican_Node_getPointer(g_mcan, IfxMultican_NodeId_0);
	node_rx = IfxMultican_Node_getPointer(g_mcan, IfxMultican_NodeId_1);

	if (cfg->loopback) {
		list_tx = 1u;
		list_rx = 2u;
		node_prepare(node_tx, btr, 1, cfg->rx_alti);
		node_prepare(node_rx, btr, 1, 0u);
	} else {
		list_tx = 1u;
		list_rx = 1u;
		node_prepare(node_tx, btr, 0, cfg->rx_alti);
		(void)node_rx;
	}

	g_mo_tx = IfxMultican_MsgObj_getPointer(g_mcan, CAN_MO_TX);
	g_mo_rx = IfxMultican_MsgObj_getPointer(g_mcan, CAN_MO_RX);
	rc = mo_init_tx(g_mo_tx, list_tx, 0x321u);
	if (rc != ULMK_OK)
		return rc;
	rc = mo_init_rx(g_mo_rx, list_rx, 0x321u);
	if (rc != ULMK_OK)
		return rc;

	node_start(node_tx);
	if (cfg->loopback)
		node_start(node_rx);
	return ULMK_OK;
}

static uint32_t hw_diag(void)
{
	Ifx_CAN_N *n0;
	Ifx_CAN_N *n1;
	uint32_t d = 0u;

	n0 = IfxMultican_Node_getPointer(g_mcan, IfxMultican_NodeId_0);
	n1 = IfxMultican_Node_getPointer(g_mcan, IfxMultican_NodeId_1);
	d |= (uint32_t)g_mcan->MCR.B.CLKSEL & 0xFu;
	d |= ((uint32_t)n0->PCR.B.LBM & 1u) << 4;
	d |= ((uint32_t)n1->PCR.B.LBM & 1u) << 5;
	d |= ((uint32_t)n0->CR.B.INIT & 1u) << 6;
	d |= ((uint32_t)n1->CR.B.INIT & 1u) << 7;
	d |= ((uint32_t)g_mo_tx->STAT.B.TXRQ & 1u) << 8;
	d |= ((uint32_t)g_mo_tx->STAT.B.MSGVAL & 1u) << 9;
	d |= ((uint32_t)g_mo_tx->STAT.B.LIST & 0xFu) << 10;
	d |= ((uint32_t)g_mo_rx->STAT.B.NEWDAT & 1u) << 14;
	d |= ((uint32_t)g_mo_rx->STAT.B.RXEN & 1u) << 15;
	d |= ((uint32_t)g_mo_rx->STAT.B.LIST & 0xFu) << 16;
	d |= ((uint32_t)n0->SR.B.TXOK & 1u) << 20;
	d |= ((uint32_t)n0->SR.B.RXOK & 1u) << 21;
	d |= ((uint32_t)n1->SR.B.RXOK & 1u) << 22;
	d |= ((uint32_t)n0->SR.B.LEC & 0x7u) << 23;
	d |= ((uint32_t)n1->SR.B.LEC & 0x7u) << 26;
	d |= ((uint32_t)g_mcan->CLC.B.DISS & 1u) << 29;
	return d;
}

static int tx_frame(const can_frame_t *f)
{
	uint32_t spin;

	if (g_mo_tx->STAT.B.TXRQ)
		return ULMK_ETIMEOUT;
	mo_clr(g_mo_tx, IfxMultican_MsgObjStatusFlag_messageValid);
	g_mo_tx->DATAL.U = (uint32_t)f->data[0] |
		((uint32_t)f->data[1] << 8) |
		((uint32_t)f->data[2] << 16) |
		((uint32_t)f->data[3] << 24);
	g_mo_tx->DATAH.U = (uint32_t)f->data[4] |
		((uint32_t)f->data[5] << 8) |
		((uint32_t)f->data[6] << 16) |
		((uint32_t)f->data[7] << 24);
	IfxMultican_MsgObj_setMessageId(g_mo_tx, f->id, FALSE);
	IfxMultican_MsgObj_setDataLengthCode(g_mo_tx,
		(IfxMultican_DataLengthCode)f->dlc);
	mo_set(g_mo_tx, IfxMultican_MsgObjStatusFlag_newData);
	mo_set(g_mo_tx, IfxMultican_MsgObjStatusFlag_messageValid);
	mo_set(g_mo_tx, IfxMultican_MsgObjStatusFlag_transmitRequest);

	for (spin = 0u; spin < 200000u; spin++) {
		if (g_mo_rx->STAT.B.NEWDAT != 0u)
			return ULMK_OK;
		if (spin > 50000u && g_mo_tx->STAT.B.TXRQ == 0u &&
		    g_mo_rx->STAT.B.NEWDAT == 0u)
			break;
	}
	return (g_mo_rx->STAT.B.NEWDAT != 0u) ? ULMK_OK : ULMK_ETIMEOUT;
}

static int rx_frame(can_frame_t *f)
{
	uint32_t lo;
	uint32_t hi;

	if (g_mo_rx->STAT.B.NEWDAT == 0u)
		return ULMK_ETIMEOUT;
	lo = g_mo_rx->DATAL.U;
	hi = g_mo_rx->DATAH.U;
	f->id = g_mo_rx->AR.B.ID >> 18;
	f->dlc = (uint8_t)g_mo_rx->FCR.B.DLC;
	if (f->dlc > 8u)
		f->dlc = 8u;
	f->data[0] = (uint8_t)(lo & 0xFFu);
	f->data[1] = (uint8_t)((lo >> 8) & 0xFFu);
	f->data[2] = (uint8_t)((lo >> 16) & 0xFFu);
	f->data[3] = (uint8_t)((lo >> 24) & 0xFFu);
	f->data[4] = (uint8_t)(hi & 0xFFu);
	f->data[5] = (uint8_t)((hi >> 8) & 0xFFu);
	f->data[6] = (uint8_t)((hi >> 16) & 0xFFu);
	f->data[7] = (uint8_t)((hi >> 24) & 0xFFu);
	mo_clr(g_mo_rx, IfxMultican_MsgObjStatusFlag_newData);
	mo_clr(g_mo_rx, IfxMultican_MsgObjStatusFlag_receivePending);
	ulmk_irq_ack(CAN_SRPN);
	return ULMK_OK;
}

static void can_server(void *arg)
{
	can_args_t *a = arg;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	can_frame_t frame;
	uint32_t bits;
	int rc;

	mapped = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_CAN_BASE,
			      CAN_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	g_mcan = (Ifx_CAN *)mapped;

	if (hw_start(&a->cfg, a->bitrate) != ULMK_OK)
		for (;;)
			;

	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;

		if (msg.label == CAN_MSG_SEND) {
			frame.id = msg.words[0];
			frame.dlc = (uint8_t)msg.words[1];
			frame.data[0] = (uint8_t)(msg.words[2] & 0xFFu);
			frame.data[1] = (uint8_t)((msg.words[2] >> 8) & 0xFFu);
			frame.data[2] = (uint8_t)((msg.words[2] >> 16) & 0xFFu);
			frame.data[3] = (uint8_t)((msg.words[2] >> 24) & 0xFFu);
			frame.data[4] = (uint8_t)(msg.words[3] & 0xFFu);
			frame.data[5] = (uint8_t)((msg.words[3] >> 8) & 0xFFu);
			frame.data[6] = (uint8_t)((msg.words[3] >> 16) & 0xFFu);
			frame.data[7] = (uint8_t)((msg.words[3] >> 24) & 0xFFu);
			if (frame.dlc > 8u) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
			} else {
				reply.words[0] = (uint32_t)tx_frame(&frame);
				if ((int)(int32_t)reply.words[0] != ULMK_OK)
					reply.words[1] = hw_diag();
			}
		} else if (msg.label == CAN_MSG_RECV) {
			if (g_mo_rx->STAT.B.NEWDAT == 0u) {
				ulmk_irq_ack(CAN_SRPN);
				bits = 0u;
				rc = ulmk_notif_wait(a->irq_notif,
						     1u << CAN_NOTIF_RX, &bits);
				if (rc != ULMK_OK) {
					reply.words[0] = (uint32_t)rc;
					ulmk_ep_reply(sender, &reply);
					continue;
				}
			}
			rc = rx_frame(&frame);
			reply.words[0] = (uint32_t)rc;
			if (rc == ULMK_OK) {
				reply.words[1] = frame.id |
					((uint32_t)frame.dlc << 16);
				reply.words[2] = (uint32_t)frame.data[0] |
					((uint32_t)frame.data[1] << 8) |
					((uint32_t)frame.data[2] << 16) |
					((uint32_t)frame.data[3] << 24);
				reply.words[3] = (uint32_t)frame.data[4] |
					((uint32_t)frame.data[5] << 8) |
					((uint32_t)frame.data[6] << 16) |
					((uint32_t)frame.data[7] << 24);
			}
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t can_init(uint8_t n, uint32_t bitrate, int loopback)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	ulmk_notif_t notif;
	int ret;

	if (n >= CAN_MAX || g_can_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	notif = ulmk_notif_create();
	if (notif == ULMK_NOTIF_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}

	g_args[n].n = n;
	g_args[n].cfg.tx_port = ULMK_BOARD_CAN_TX_PORT;
	g_args[n].cfg.tx_pin = ULMK_BOARD_CAN_TX_PIN;
	g_args[n].cfg.tx_alt = ULMK_BOARD_CAN_TX_ALT;
	g_args[n].cfg.rx_port = ULMK_BOARD_CAN_RX_PORT;
	g_args[n].cfg.rx_pin = ULMK_BOARD_CAN_RX_PIN;
	g_args[n].cfg.rx_alti = ULMK_BOARD_CAN_RX_ALTI;
	g_args[n].cfg.nen_port = ULMK_BOARD_CAN_NEN_PORT;
	g_args[n].cfg.nen_pin = ULMK_BOARD_CAN_NEN_PIN;
	g_args[n].cfg.loopback = loopback ? 1u : 0u;
	g_args[n].bitrate = bitrate ? bitrate : 500000u;
	g_args[n].ep = ep;
	g_args[n].irq_notif = notif;

	ret = ulmk_irq_bind_hw(CAN_SRPN, notif, CAN_NOTIF_RX, CAN_SRC);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(CAN_SRPN);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name = "can";
	attr.entry = can_server;
	attr.arg = &g_args[n];
	attr.priority = 3u;
	attr.stack_size = CAN_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	g_can_eps[n] = ep;
	return tid;
}
