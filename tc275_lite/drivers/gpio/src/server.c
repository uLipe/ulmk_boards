/* SPDX-License-Identifier: MIT */
/*
 * gpio server — PORT access via iLLD SFR / IfxPort inlines.
 *
 * Subscribe is non-blocking: ep_call registers (notif, bit) and returns.
 * Edge IRQs come from GTM TIM (Lite Kit Button1 P00.7 → TIM2 CH6).  The
 * server waits on ep_recv_or_notif; on IRQ it defers by signaling every
 * matching subscriber — no pin polling.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"
#include "board_config.h"
#include "drivers/common/illd_port.h"
#include "IfxGtm_reg.h"
#include "IfxGtm_bf.h"
#include "Gtm/Std/IfxGtm_Tim.h"

#define GPIO_STACK_SIZE	1536u
#define GPIO_MAX_SUBS	4u
#define GPIO_PORT_SLOTS	8u
#define GPIO_IRQ_BIT	0u
#define GPIO_SRPN	ULMK_BOARD_IRQ_GPIO_BTN
#define GPIO_SRC	ULMK_BOARD_SRC_GTM_TIM2_6

/* Lite Kit Button1 — only IRQ-capable subscribe pin for now. */
#define GPIO_BTN_ENC \
	GPIO_PIN(ULMK_BOARD_BUTTON_PORT, ULMK_BOARD_BUTTON_PIN)

#define GPIO_TIM_IDX		2u
#define GPIO_TIM_CH		6u
#define GPIO_TIM_TIN_SEL	1u	/* TIN16 */
#define GPIO_CMU_MAP_SIZE	0x100u
#define GPIO_GTM_CMU_BASE	ULMK_BOARD_GTM_CMU_BASE

struct gpio_sub {
	uint16_t     pin;
	uint32_t     edge;
	ulmk_notif_t notif;
	uint32_t     bit;
	int          last;
	int          active;
};

struct port_slot {
	uint8_t port;
	Ifx_P  *mod;
	int     used;
};

static struct gpio_sub g_subs[GPIO_MAX_SUBS]
	__attribute__((section(".user_bss")));
static struct port_slot g_ports[GPIO_PORT_SLOTS]
	__attribute__((section(".user_bss")));
ulmk_ep_t g_gpio_ep;
static ulmk_notif_t g_irq_notif __attribute__((section(".user_bss")));
static Ifx_GTM_TIM_CH *g_tim_ch __attribute__((section(".user_bss")));
static uint8_t g_tim_armed __attribute__((section(".user_bss")));
static volatile uint32_t *g_cmu_clk_en __attribute__((section(".user_bss")));

static Ifx_P *map_port(uint8_t port)
{
	uint32_t i;
	Ifx_P *mod;
	void *mapped;

	for (i = 0u; i < GPIO_PORT_SLOTS; i++) {
		if (g_ports[i].used && g_ports[i].port == port)
			return g_ports[i].mod;
	}
	mod = illd_port_module(port);
	if (!mod)
		return NULL;
	mapped = ulmk_mem_map((void *)mod, ILLD_PORT_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		return NULL;
	mod = (Ifx_P *)mapped;
	for (i = 0u; i < GPIO_PORT_SLOTS; i++) {
		if (!g_ports[i].used) {
			g_ports[i].port = port;
			g_ports[i].mod = mod;
			g_ports[i].used = 1;
			return mod;
		}
	}
	return mod;
}

static int do_config(uint16_t enc, uint32_t dir, uint32_t pull, uint32_t alt)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	Ifx_P *mod;
	IfxPort_Mode mode;

	mod = map_port(port);
	if (!mod)
		return ULMK_EINVAL;

	if (dir == GPIO_DIR_IN) {
		if (pull == GPIO_PULL_UP)
			mode = IfxPort_Mode_inputPullUp;
		else if (pull == GPIO_PULL_DOWN)
			mode = IfxPort_Mode_inputPullDown;
		else
			mode = IfxPort_Mode_inputNoPullDevice;
	} else {
		if (alt == 0u)
			mode = IfxPort_Mode_outputPushPullGeneral;
		else if (alt == 1u)
			mode = IfxPort_Mode_outputPushPullAlt1;
		else if (alt == 2u)
			mode = IfxPort_Mode_outputPushPullAlt2;
		else if (alt == 3u)
			mode = IfxPort_Mode_outputPushPullAlt3;
		else if (alt == 4u)
			mode = IfxPort_Mode_outputPushPullAlt4;
		else if (alt == 5u)
			mode = IfxPort_Mode_outputPushPullAlt5;
		else if (alt == 6u)
			mode = IfxPort_Mode_outputPushPullAlt6;
		else
			mode = IfxPort_Mode_outputPushPullAlt7;
	}
	illd_port_set_mode(mod, pin, mode);
	return ULMK_OK;
}

static int do_set(uint16_t enc, int value)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	Ifx_P *mod;

	mod = map_port(port);
	if (!mod)
		return ULMK_EINVAL;
	if (value)
		IfxPort_setPinHigh(mod, pin);
	else
		IfxPort_setPinLow(mod, pin);
	return ULMK_OK;
}

static int do_get(uint16_t enc, int *value)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	Ifx_P *mod;

	mod = map_port(port);
	if (!mod || !value)
		return ULMK_EINVAL;
	*value = IfxPort_getPinState(mod, pin) ? 1 : 0;
	return ULMK_OK;
}

static void tim_ack(void)
{
	if (g_tim_ch) {
		g_tim_ch->IRQ_NOTIFY.U =
			1u << IFX_GTM_TIM_CH_IRQ_NOTIFY_NEWVAL_OFF;
	}
	ulmk_irq_ack(GPIO_SRPN);
}

static int tim_arm(uint32_t edge)
{
	Ifx_GTM_TIM_CH *ch;
	uint32_t shift;
	uint32_t mask;
	uint32_t val;
	uint32_t stride;

	if (g_tim_armed)
		return ULMK_OK;

	if (!g_cmu_clk_en) {
		g_cmu_clk_en = (volatile uint32_t *)ulmk_mem_map(
			(void *)(uintptr_t)GPIO_GTM_CMU_BASE, GPIO_CMU_MAP_SIZE,
			ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
		if (!g_cmu_clk_en)
			return ULMK_ENOMEM;
	}
	/* GCLK = cluster clock; enable CMU CLK0 for TIM capture. */
	g_cmu_clk_en[1] = 1u; /* GCLK_NUM @ +0x4 */
	g_cmu_clk_en[2] = 1u; /* GCLK_DEN @ +0x8 */
	g_cmu_clk_en[0] = 0x00000002u; /* IFXGTM_CMU_CLKEN_CLK0 */

	/* TIM2 INSEL CH6 ← TIN16 (select=1). */
	shift = GPIO_TIM_CH * 4u;
	mask = 0xFu << shift;
	val = (uint32_t)GPIO_TIM_TIN_SEL << shift;
	GTM_INOUTSEL_TIM2_INSEL.U =
		(GTM_INOUTSEL_TIM2_INSEL.U & ~mask) | val;

	ch = IfxGtm_Tim_getChannel(&MODULE_GTM.TIM[GPIO_TIM_IDX],
				   (IfxGtm_Tim_Ch)GPIO_TIM_CH);
	g_tim_ch = ch;

	ch->CTRL.U = 0u;
	ch->CTRL.B.TIM_MODE = IfxGtm_Tim_Mode_inputEvent;
	ch->CTRL.B.CLK_SEL = 0u; /* CMU CLK0 */
	if (edge == GPIO_EVT_BOTH) {
		ch->CTRL.B.ISL = 1u;
		ch->CTRL.B.DSL = 0u;
	} else if (edge == GPIO_EVT_RISING) {
		ch->CTRL.B.ISL = 0u;
		ch->CTRL.B.DSL = 1u;
	} else {
		/* falling (active-low button press) */
		ch->CTRL.B.ISL = 0u;
		ch->CTRL.B.DSL = 0u;
	}
	ch->CTRL.B.CICTRL = 0u; /* current channel */
	ch->CTRL.B.CNTS_SEL = 0u;
	ch->CTRL.B.GPR0_SEL = 0u;
	ch->CTRL.B.GPR1_SEL = 0u;

	stride = IFX_GTM_TIM_IN_SRC_MODE_1_OFF - IFX_GTM_TIM_IN_SRC_MODE_0_OFF;
	MODULE_GTM.TIM[GPIO_TIM_IDX].IN_SRC.U =
		1u << (IFX_GTM_TIM_IN_SRC_MODE_0_OFF + GPIO_TIM_CH * stride);
	MODULE_GTM.TIM[GPIO_TIM_IDX].IN_SRC.U =
		1u << (IFX_GTM_TIM_IN_SRC_VAL_0_OFF + GPIO_TIM_CH * stride);

	ch->IRQ_MODE.B.IRQ_MODE = 3u; /* singlePulse — one SRC assert per edge */
	ch->IRQ_EN.U = 0u;
	ch->IRQ_EN.B.NEWVAL_IRQ_EN = 1u;
	ch->IRQ_NOTIFY.U = 0x1Fu;
	ch->CTRL.B.TIM_EN = 1u;

	tim_ack();
	g_tim_armed = 1u;
	return ULMK_OK;
}

static void defer_subs(uint16_t enc)
{
	uint32_t i;
	int level;
	int ok;
	int rose;
	int fell;

	ok = (do_get(enc, &level) == ULMK_OK);
	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active || g_subs[i].pin != enc)
			continue;
		if (!ok) {
			(void)ulmk_notif_signal(g_subs[i].notif,
						1u << g_subs[i].bit);
			continue;
		}
		rose = (level && !g_subs[i].last);
		fell = (!level && g_subs[i].last);
		/*
		 * Forced IRQ (HIL) may leave the level unchanged — still
		 * notify every subscriber on this pin.
		 */
		if ((!rose && !fell) ||
		    (rose && (g_subs[i].edge & GPIO_EVT_RISING)) ||
		    (fell && (g_subs[i].edge & GPIO_EVT_FALLING)))
			(void)ulmk_notif_signal(g_subs[i].notif,
						1u << g_subs[i].bit);
		g_subs[i].last = level;
	}
}

static int do_subscribe(uint16_t enc, uint32_t edge, ulmk_notif_t n,
			uint32_t bit)
{
	uint32_t i;
	int level;
	int rc;

	if (edge == GPIO_EVT_NONE)
		return ULMK_EINVAL;
	/* IRQ path is wired for Button1 (P00.7 → TIM2 CH6) only. */
	if (enc != GPIO_BTN_ENC)
		return ULMK_EINVAL;
	if (do_get(enc, &level) != ULMK_OK)
		return ULMK_EINVAL;

	rc = tim_arm(edge);
	if (rc != ULMK_OK)
		return rc;

	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active) {
			g_subs[i].pin = enc;
			g_subs[i].edge = edge;
			g_subs[i].notif = n;
			g_subs[i].bit = bit;
			g_subs[i].last = level;
			g_subs[i].active = 1;
			return ULMK_OK;
		}
	}
	return ULMK_ENOMEM;
}

static void gpio_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint32_t bits;
	int value;
	int rc;

	(void)arg;
	for (;;) {
		bits = 0u;
		sender = ULMK_TID_INVALID;
		rc = ulmk_ep_recv_or_notif(g_gpio_ep, g_irq_notif,
					   1u << GPIO_IRQ_BIT, &msg, &sender,
					   &bits);
		if (rc == 1) {
			tim_ack();
			defer_subs(GPIO_BTN_ENC);
			continue;
		}
		if (rc != 0)
			continue;

		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		switch (msg.label) {
		case GPIO_MSG_CONFIG:
			reply.words[0] = (uint32_t)do_config(
				(uint16_t)msg.words[0], msg.words[1],
				msg.words[2], msg.words[3]);
			break;
		case GPIO_MSG_SET:
			reply.words[0] = (uint32_t)do_set(
				(uint16_t)msg.words[0], (int)msg.words[1]);
			break;
		case GPIO_MSG_GET:
			value = 0;
			reply.words[0] = (uint32_t)do_get(
				(uint16_t)msg.words[0], &value);
			reply.words[1] = (uint32_t)value;
			break;
		case GPIO_MSG_SUBSCRIBE:
			reply.words[0] = (uint32_t)do_subscribe(
				(uint16_t)msg.words[0], msg.words[1],
				(ulmk_notif_t)msg.words[2], msg.words[3]);
			break;
		case GPIO_MSG_IRQ_KICK:
			/* Same defer path as TIM IRQ (HIL without SRC SETR). */
			defer_subs(GPIO_BTN_ENC);
			reply.words[0] = (uint32_t)ULMK_OK;
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t gpio_init(void)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	int ret;

	if (g_gpio_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_gpio_ep = ulmk_ep_create();
	if (g_gpio_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_irq_notif = ulmk_notif_create();
	if (g_irq_notif == ULMK_NOTIF_INVALID) {
		ulmk_ep_destroy(g_gpio_ep);
		g_gpio_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}

	ret = ulmk_irq_bind_hw(GPIO_SRPN, g_irq_notif, GPIO_IRQ_BIT,
			       (uintptr_t)GPIO_SRC);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(GPIO_SRPN);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name       = "gpio";
	attr.entry      = gpio_server;
	attr.arg        = NULL;
	attr.priority   = 2u;
	attr.stack_size = GPIO_STACK_SIZE;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(g_gpio_ep);
		g_gpio_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
