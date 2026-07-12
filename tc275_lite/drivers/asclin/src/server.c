/* SPDX-License-Identifier: MIT */
/*
 * asclin server — UART via iLLD inlines; TX/RX wait on SRC IRQ + notif.
 *
 * CLC enabled in board_init.  No TX/RX FIFO busy-wait loops.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "asclin_internal.h"
#include "board_config.h"
#include "drivers/common/illd_port.h"
#include "IfxAsclin_reg.h"
#include "Asclin/Std/IfxAsclin.h"

#define ASCLIN_STACK_SIZE	2048u
#define ASCLIN_MAP_SIZE		0x100u
#define ASCLIN_NOTIF_TX		0u
#define ASCLIN_NOTIF_RX		1u
#define ASCLIN_CLK_SETTLE	100000u

typedef struct {
	uint8_t       n;
	asclin_pins_t pins;
	uint32_t      baud;
	uint32_t      fa_hz;
	ulmk_ep_t     ep;
	ulmk_notif_t  irq_notif;
	uint8_t       tx_srpn;
	uint8_t       rx_srpn;
	uintptr_t     tx_src;
	uintptr_t     rx_src;
} asclin_args_t;

ulmk_ep_t g_asclin_eps[ASCLIN_MAX];
static asclin_args_t g_args[ASCLIN_MAX] __attribute__((section(".user_bss")));

static Ifx_ASCLIN *asclin_module(uint8_t n)
{
	switch (n) {
	case 0u: return &MODULE_ASCLIN0;
	case 1u: return &MODULE_ASCLIN1;
	case 2u: return &MODULE_ASCLIN2;
	case 3u: return &MODULE_ASCLIN3;
	default: return NULL;
	}
}

static void asclin_set_clock(Ifx_ASCLIN *asc, IfxAsclin_ClockSource src)
{
	uint32_t spin = 0u;

	asc->CSR.B.CLKSEL = (uint8_t)src;
	if (src == IfxAsclin_ClockSource_noClock) {
		while (IfxAsclin_getClockStatus(asc) != 0u &&
		       spin++ < ASCLIN_CLK_SETTLE)
			;
	} else {
		while (IfxAsclin_getClockStatus(asc) != 1u &&
		       spin++ < ASCLIN_CLK_SETTLE)
			;
	}
}

static void pinmux(const asclin_pins_t *p)
{
	Ifx_P *tx;
	Ifx_P *rx;
	void *m;
	IfxPort_Mode mode;

	if (p->tx_alt != 0u) {
		tx = illd_port_module(p->tx_port);
		if (tx) {
			m = ulmk_mem_map((void *)tx, ILLD_PORT_MAP_SIZE,
					 ULMK_PERM_READ | ULMK_PERM_WRITE,
					 ULMK_MMAP_PERIPH);
			if (m) {
				switch (p->tx_alt) {
				case 1u: mode = IfxPort_Mode_outputPushPullAlt1; break;
				case 2u: mode = IfxPort_Mode_outputPushPullAlt2; break;
				case 3u: mode = IfxPort_Mode_outputPushPullAlt3; break;
				default: mode = IfxPort_Mode_outputPushPullAlt2; break;
				}
				illd_port_set_mode((Ifx_P *)m, p->tx_pin, mode);
			}
		}
	}
	rx = illd_port_module(p->rx_port);
	if (rx) {
		m = ulmk_mem_map((void *)rx, ILLD_PORT_MAP_SIZE,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_PERIPH);
		if (m)
			illd_port_set_mode((Ifx_P *)m, p->rx_pin,
					   IfxPort_Mode_inputPullUp);
	}
}

static void hw_set_baud(Ifx_ASCLIN *asc, uint32_t baud, uint32_t fa_hz)
{
	uint32_t denom = 3072u;
	uint32_t numer;

	numer = (uint32_t)(((uint64_t)baud * denom * 16u + (fa_hz / 2u)) / fa_hz);
	if (numer == 0u)
		numer = 1u;
	if (numer > 0xFFFu)
		numer = 0xFFFu;
	IfxAsclin_setDenominator(asc, (uint16_t)denom);
	IfxAsclin_setNumerator(asc, (uint16_t)numer);
}

static void hw_init(Ifx_ASCLIN *asc, const asclin_pins_t *pins,
		    uint32_t baud, uint32_t fa_hz)
{
	asclin_set_clock(asc, IfxAsclin_ClockSource_noClock);

	IfxAsclin_setRxInput(asc, (IfxAsclin_RxInputSelect)(pins->rx_alti & 7u));

	IfxAsclin_setPrescaler(asc, 1u);
	IfxAsclin_setOversampling(asc, IfxAsclin_OversamplingFactor_16);
	IfxAsclin_setSamplePointPosition(asc, IfxAsclin_SamplePointPosition_9);
	IfxAsclin_setSampleMode(asc, IfxAsclin_SamplesPerBit_three);

	IfxAsclin_setFrameMode(asc, IfxAsclin_FrameMode_initialise);
	IfxAsclin_setStopBit(asc, IfxAsclin_StopBit_1);
	IfxAsclin_enableParity(asc, FALSE);
	IfxAsclin_setShiftDirection(asc, IfxAsclin_ShiftDirection_lsbFirst);
	IfxAsclin_setDataLength(asc, IfxAsclin_DataLength_8);

	hw_set_baud(asc, baud, fa_hz);

	IfxAsclin_setTxFifoInletWidth(asc, IfxAsclin_TxFifoInletWidth_1);
	IfxAsclin_setRxFifoOutletWidth(asc, IfxAsclin_RxFifoOutletWidth_1);
	/* TFL when fill ≤15 (space for ≥1 byte); RFL when ≥1 byte. */
	IfxAsclin_setTxFifoInterruptLevel(asc, IfxAsclin_TxFifoInterruptLevel_15);
	IfxAsclin_setRxFifoInterruptLevel(asc, IfxAsclin_RxFifoInterruptLevel_1);

	/* Frame mode ASC while clock still off (MODE is clock-gated). */
	IfxAsclin_setFrameMode(asc, IfxAsclin_FrameMode_asc);
	asclin_set_clock(asc, IfxAsclin_ClockSource_ascFastClock);

	IfxAsclin_disableAllFlags(asc);
	IfxAsclin_clearAllFlags(asc);

	/*
	 * TXFIFOCON/RXFIFOCON: FLUSH is write-only.  Bitfield RMW after a
	 * non-empty FIFO can drop ENO/ENI — write the whole control word
	 * after flush.  INW/OUTW must stay 1 (byte); 0 means 0 bytes/access.
	 */
	asc->TXFIFOCON.U = 1u; /* FLUSH */
	asc->TXFIFOCON.U = (15u << 8) | (1u << 6) | (1u << 1);
	asc->RXFIFOCON.U = 1u; /* FLUSH */
	asc->RXFIFOCON.U = (1u << 8) | (1u << 6) | (1u << 1);
}

static int wait_space(asclin_args_t *a, Ifx_ASCLIN *asc)
{
	uint32_t bits;
	int ret;

	if (IfxAsclin_getTxFifoFillLevel(asc) < 16u)
		return ULMK_OK;

	IfxAsclin_clearTxFifoFillLevelFlag(asc);
	ulmk_irq_ack(a->tx_srpn);
	IfxAsclin_enableTxFifoFillLevelFlag(asc, TRUE);

	bits = 0u;
	ret = ulmk_notif_wait(a->irq_notif, 1u << ASCLIN_NOTIF_TX, &bits);

	IfxAsclin_enableTxFifoFillLevelFlag(asc, FALSE);
	IfxAsclin_clearTxFifoFillLevelFlag(asc);
	ulmk_irq_ack(a->tx_srpn);
	return ret;
}

static int hw_tx(asclin_args_t *a, Ifx_ASCLIN *asc, uint8_t byte)
{
	int ret;

	ret = wait_space(a, asc);
	if (ret != ULMK_OK)
		return ret;
	IfxAsclin_writeTxData(asc, byte);
	return ULMK_OK;
}

static int hw_rx(asclin_args_t *a, Ifx_ASCLIN *asc, uint8_t *out, int blocking)
{
	uint32_t bits;
	int ret;

	if (IfxAsclin_getRxFifoFillLevel(asc) > 0u) {
		*out = (uint8_t)IfxAsclin_readRxData(asc);
		return ULMK_OK;
	}
	if (!blocking)
		return ULMK_ETIMEOUT;

	IfxAsclin_clearRxFifoFillLevelFlag(asc);
	ulmk_irq_ack(a->rx_srpn);
	IfxAsclin_enableRxFifoFillLevelFlag(asc, TRUE);

	bits = 0u;
	ret = ulmk_notif_wait(a->irq_notif, 1u << ASCLIN_NOTIF_RX, &bits);

	IfxAsclin_enableRxFifoFillLevelFlag(asc, FALSE);
	IfxAsclin_clearRxFifoFillLevelFlag(asc);
	ulmk_irq_ack(a->rx_srpn);

	if (ret != ULMK_OK)
		return ret;
	if (IfxAsclin_getRxFifoFillLevel(asc) == 0u)
		return ULMK_ETIMEOUT;
	*out = (uint8_t)IfxAsclin_readRxData(asc);
	return ULMK_OK;
}

static void asclin_server(void *arg)
{
	asclin_args_t *a = (asclin_args_t *)arg;
	Ifx_ASCLIN *asc;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t b;

	asc = asclin_module(a->n);
	if (!asc)
		for (;;)
			;

	mapped = ulmk_mem_map((void *)asc, ASCLIN_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	asc = (Ifx_ASCLIN *)mapped;

	pinmux(&a->pins);
	hw_init(asc, &a->pins, a->baud, a->fa_hz);

	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		b = 0u;
		switch (msg.label) {
		case ASCLIN_MSG_TX_BYTE:
			reply.words[0] = (uint32_t)hw_tx(a, asc,
						(uint8_t)msg.words[0]);
			break;
		case ASCLIN_MSG_RX_BYTE:
			reply.words[0] = (uint32_t)hw_rx(a, asc, &b, 1);
			reply.words[1] = b;
			break;
		case ASCLIN_MSG_RX_BYTE_NB:
			reply.words[0] = (uint32_t)hw_rx(a, asc, &b, 0);
			reply.words[1] = b;
			break;
		case ASCLIN_MSG_SET_BAUD:
			hw_set_baud(asc, msg.words[0], a->fa_hz);
			a->baud = msg.words[0];
			reply.words[0] = (uint32_t)ULMK_OK;
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t asclin_init(uint8_t n, const asclin_pins_t *pins,
		       uint32_t baud, uint32_t fa_hz)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	ulmk_notif_t notif;
	int ret;

	if (n >= ASCLIN_MAX || !pins)
		return ULMK_TID_INVALID;
	if (g_asclin_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	/* IRQ wiring is currently defined for ASCLIN0 (Lite Kit console). */
	if (n != 0u)
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
	g_args[n].pins = *pins;
	g_args[n].baud = baud;
	g_args[n].fa_hz = fa_hz;
	g_args[n].ep = ep;
	g_args[n].irq_notif = notif;
	g_args[n].tx_srpn = ULMK_BOARD_IRQ_ASCLIN0_TX;
	g_args[n].rx_srpn = ULMK_BOARD_IRQ_ASCLIN0_RX;
	g_args[n].tx_src = (uintptr_t)ULMK_BOARD_SRC_ASCLIN0_TX;
	g_args[n].rx_src = (uintptr_t)ULMK_BOARD_SRC_ASCLIN0_RX;

	ret = ulmk_irq_bind_hw(g_args[n].tx_srpn, notif, ASCLIN_NOTIF_TX,
			       g_args[n].tx_src);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_bind_hw(g_args[n].rx_srpn, notif, ASCLIN_NOTIF_RX,
			       g_args[n].rx_src);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(g_args[n].tx_srpn);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(g_args[n].rx_srpn);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name       = "asclin";
	attr.entry      = asclin_server;
	attr.arg        = &g_args[n];
	attr.priority   = 1u;
	attr.stack_size = ASCLIN_STACK_SIZE;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	g_asclin_eps[n] = ep;
	return tid;
}
