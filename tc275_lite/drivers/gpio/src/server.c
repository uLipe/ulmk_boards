/* SPDX-License-Identifier: MIT */
/*
 * gpio server — PORT access via iLLD SFR / IfxPort inlines.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"
#include "drivers/common/illd_port.h"

#define GPIO_STACK_SIZE	1536u
#define GPIO_MAX_SUBS	4u
#define GPIO_PORT_SLOTS	8u

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

static int do_subscribe(uint16_t enc, uint32_t edge, ulmk_notif_t n,
			uint32_t bit)
{
	uint32_t i;
	int level;

	if (edge == GPIO_EVT_NONE)
		return ULMK_EINVAL;
	if (do_get(enc, &level) != ULMK_OK)
		return ULMK_EINVAL;
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

static void poll_subs(void)
{
	uint32_t i;
	int level;
	int rose;
	int fell;

	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active)
			continue;
		if (do_get(g_subs[i].pin, &level) != ULMK_OK)
			continue;
		rose = (level && !g_subs[i].last);
		fell = (!level && g_subs[i].last);
		g_subs[i].last = level;
		if ((rose && (g_subs[i].edge & GPIO_EVT_RISING)) ||
		    (fell && (g_subs[i].edge & GPIO_EVT_FALLING)))
			(void)ulmk_notif_signal(g_subs[i].notif, g_subs[i].bit);
	}
}

static void gpio_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	int value;

	(void)arg;
	for (;;) {
		poll_subs();
		if (ulmk_ep_recv(g_gpio_ep, &msg, &sender) != ULMK_OK)
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

	if (g_gpio_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_gpio_ep = ulmk_ep_create();
	if (g_gpio_ep == ULMK_EP_INVALID)
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
	return tid;
}
