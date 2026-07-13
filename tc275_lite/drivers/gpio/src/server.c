/* SPDX-License-Identifier: MIT */
/*
 * gpio server — digital I/O via pinmux-owned PORT maps + SCU ERU IRQ.
 *
 * PORTs are never mem_mapped here — pinmux_init() / pinmux_port() own that.
 * SCU ERU is mapped once in gpio_init().
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"
#include "Port/Std/IfxPort.h"
#include "Scu/Std/IfxScuEru.h"

#define GPIO_STACK_SIZE		1536u
#define GPIO_MAX_SUBS		4u
#define GPIO_IRQ_BIT		0u
#define GPIO_SRPN		ULMK_BOARD_IRQ_GPIO_ERU
#define GPIO_SRC		ULMK_BOARD_SRC_SCU_ERU0
#define GPIO_SCU_ERU_BASE	0xF0036210u
#define GPIO_SCU_ERU_SIZE	0x40u
#define GPIO_ERU_CH_MAX		8u

struct gpio_sub {
	uint16_t     pin;
	uint32_t     edge;
	ulmk_notif_t notif;
	uint32_t     bit;
	int          active;
};

struct eru_pin {
	uint8_t port;
	uint8_t pin;
	uint8_t ch;
	uint8_t sel;
};

static const struct eru_pin eru_pins[] = {
	{ 15u,  4u, 0u, 0u },
	{ 10u,  7u, 0u, 2u },
	{ 14u,  3u, 1u, 0u },
	{ 10u,  8u, 1u, 2u },
	{ 10u,  2u, 2u, 0u },
	{  2u,  1u, 2u, 1u },
	{  0u,  4u, 2u, 2u },
	{ 10u,  3u, 3u, 0u },
	{ 14u,  1u, 3u, 1u },
	{  2u,  0u, 3u, 2u },
	{ 33u,  7u, 4u, 0u },
	{ 15u,  5u, 4u, 3u },
	{ 15u,  8u, 5u, 0u },
	{ 20u,  0u, 6u, 0u },
	{ 11u, 10u, 6u, 3u },
	{ 20u,  9u, 7u, 0u },
	{ 15u,  1u, 7u, 2u },
};

static struct gpio_sub g_subs[GPIO_MAX_SUBS]
	__attribute__((section(".user_bss")));
static uint16_t g_eru_enc[GPIO_ERU_CH_MAX]
	__attribute__((section(".user_bss")));
static uint8_t g_eru_armed_mask __attribute__((section(".user_bss")));
static uint8_t g_ogu0_armed __attribute__((section(".user_bss")));
static uint8_t g_scu_mapped __attribute__((section(".user_bss")));

ulmk_ep_t g_gpio_eps[GPIO_MAX];
static ulmk_notif_t g_irq_notif __attribute__((section(".user_bss")));
static uint8_t g_eru_started __attribute__((section(".user_bss")));

static int do_config(uint16_t enc, uint32_t dir, uint32_t pull)
{
	pinmux_cfg_t cfg;

	cfg.port  = (uint8_t)(enc >> 8);
	cfg.pin   = (uint8_t)(enc & 0xFFu);
	cfg.dir   = (dir == GPIO_DIR_IN) ? PINMUX_DIR_IN : PINMUX_DIR_OUT;
	cfg.pull  = (uint8_t)pull;
	cfg.alt   = PINMUX_ALT_GPIO;
	cfg.flags = 0u;
	return pinmux_apply(&cfg);
}

static int do_set(uint16_t enc, int value)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	Ifx_P *mod;

	mod = pinmux_port(port);
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

	mod = pinmux_port(port);
	if (!mod || !value)
		return ULMK_EINVAL;
	*value = IfxPort_getPinState(mod, pin) ? 1 : 0;
	return ULMK_OK;
}

static const struct eru_pin *eru_lookup(uint16_t enc)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	uint32_t i;

	for (i = 0u; i < (uint32_t)(sizeof(eru_pins) / sizeof(eru_pins[0]));
	     i++) {
		if (eru_pins[i].port == port && eru_pins[i].pin == pin)
			return &eru_pins[i];
	}
	return NULL;
}

static void defer_subs(uint16_t enc)
{
	uint32_t i;

	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active || g_subs[i].pin != enc)
			continue;
		(void)ulmk_notif_signal(g_subs[i].notif,
					1u << g_subs[i].bit);
	}
}

static int eru_arm(uint16_t enc, uint32_t edge)
{
	const struct eru_pin *rp;
	IfxScuEru_InputChannel ch;

	rp = eru_lookup(enc);
	if (!rp)
		return ULMK_EINVAL;

	ch = (IfxScuEru_InputChannel)rp->ch;
	IfxScuEru_selectExternalInput(
		ch, (IfxScuEru_ExternalInputSelection)rp->sel);

	IfxScuEru_disableRisingEdgeDetection(ch);
	IfxScuEru_disableFallingEdgeDetection(ch);
	if (edge & GPIO_EVT_RISING)
		IfxScuEru_enableRisingEdgeDetection(ch);
	if (edge & GPIO_EVT_FALLING)
		IfxScuEru_enableFallingEdgeDetection(ch);

	IfxScuEru_enableTriggerPulse(ch);
	IfxScuEru_connectTrigger(ch, IfxScuEru_InputNodePointer_0);

	if (!g_ogu0_armed) {
		IfxScuEru_setInterruptGatingPattern(
			IfxScuEru_OutputChannel_0,
			IfxScuEru_InterruptGatingPattern_alwaysActive);
		g_ogu0_armed = 1u;
	}

	IfxScuEru_clearEventFlag(ch);
	g_eru_enc[rp->ch] = enc;
	g_eru_armed_mask |= (uint8_t)(1u << rp->ch);
	return ULMK_OK;
}

static int do_subscribe(uint16_t enc, uint32_t edge, ulmk_notif_t n,
			uint32_t bit)
{
	uint32_t i;
	int rc;

	if (edge == GPIO_EVT_NONE || bit > 31u)
		return ULMK_EINVAL;
	if (!eru_lookup(enc))
		return ULMK_EINVAL;

	rc = eru_arm(enc, edge);
	if (rc != ULMK_OK)
		return rc;

	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active) {
			g_subs[i].pin = enc;
			g_subs[i].edge = edge;
			g_subs[i].notif = n;
			g_subs[i].bit = bit;
			g_subs[i].active = 1;
			return ULMK_OK;
		}
	}
	return ULMK_ENOMEM;
}

static void gpio_irq_server(void *arg)
{
	uint32_t bits;
	uint32_t ch;
	int rc;

	(void)arg;
	for (;;) {
		bits = 0u;
		rc = ulmk_notif_wait(g_irq_notif, 1u << GPIO_IRQ_BIT, &bits);
		if (rc != ULMK_OK)
			continue;

		for (ch = 0u; ch < GPIO_ERU_CH_MAX; ch++) {
			if (!IfxScuEru_getEventFlagStatus(
				    (IfxScuEru_InputChannel)ch))
				continue;
			IfxScuEru_clearEventFlag((IfxScuEru_InputChannel)ch);
			if (g_eru_armed_mask & (uint8_t)(1u << ch))
				defer_subs(g_eru_enc[ch]);
		}
		ulmk_irq_ack(GPIO_SRPN);
	}
}

static void gpio_ipc_server(void *arg)
{
	ulmk_ep_t ep = (ulmk_ep_t)(uintptr_t)arg;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	int value;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;

		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		switch (msg.label) {
		case GPIO_MSG_CONFIG:
			reply.words[0] = (uint32_t)do_config(
				(uint16_t)msg.words[0], msg.words[1],
				msg.words[2]);
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
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t gpio_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t ipc_tid;
	ulmk_tid_t irq_tid;
	ulmk_ep_t ep;
	int ret;

	if (n >= GPIO_MAX)
		return ULMK_TID_INVALID;
	if (g_gpio_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	if (!g_scu_mapped) {
		if (!ulmk_mem_map((void *)(uintptr_t)GPIO_SCU_ERU_BASE,
				  GPIO_SCU_ERU_SIZE,
				  ULMK_PERM_READ | ULMK_PERM_WRITE,
				  ULMK_MMAP_PERIPH)) {
			ulmk_ep_destroy(ep);
			return ULMK_TID_INVALID;
		}
		g_scu_mapped = 1u;
	}

	if (!g_eru_started) {
		g_irq_notif = ulmk_notif_create();
		if (g_irq_notif == ULMK_NOTIF_INVALID) {
			ulmk_ep_destroy(ep);
			return ULMK_TID_INVALID;
		}
		ret = ulmk_irq_bind_hw(GPIO_SRPN, g_irq_notif, GPIO_IRQ_BIT,
				       (uintptr_t)GPIO_SRC);
		if (ret != ULMK_OK)
			return ULMK_TID_INVALID;
		ret = ulmk_irq_enable(GPIO_SRPN);
		if (ret != ULMK_OK)
			return ULMK_TID_INVALID;

		attr.name       = "gpio_irq";
		attr.entry      = gpio_irq_server;
		attr.arg        = NULL;
		attr.priority   = 1u;
		attr.stack_size = GPIO_STACK_SIZE;
		attr.privilege  = ULMK_PRIV_DRIVER;
		irq_tid = ulmk_thread_create(&attr);
		if (irq_tid == ULMK_TID_INVALID) {
			ulmk_ep_destroy(ep);
			return ULMK_TID_INVALID;
		}
		ulmk_cap_grant(irq_tid, ULMK_CAP_MAP_PERIPH);
		ulmk_cap_grant(irq_tid, ULMK_CAP_IRQ);
		g_eru_started = 1u;
	}

	attr.name       = "gpio_ipc";
	attr.entry      = gpio_ipc_server;
	attr.arg        = (void *)(uintptr_t)ep;
	attr.priority   = 2u;
	attr.stack_size = GPIO_STACK_SIZE;
	attr.privilege  = ULMK_PRIV_DRIVER;
	ipc_tid = ulmk_thread_create(&attr);
	if (ipc_tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(ipc_tid, ULMK_CAP_MAP_PERIPH);
	g_gpio_eps[n] = ep;
	return ipc_tid;
}
