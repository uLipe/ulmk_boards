/* SPDX-License-Identifier: MIT */
/*
 * board_gpio.c — PORT GPIO IPC server (userspace).
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_gpio.h"
#include "drivers/port/port_regs.h"

#define GPIO_MSG_CONFIG		1u
#define GPIO_MSG_SET		2u
#define GPIO_MSG_GET		3u
#define GPIO_MSG_SUBSCRIBE	4u

#define GPIO_MAX_SUBS		4u
#define GPIO_PORT_SLOTS		8u

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
	void   *base;
	int     used;
};

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static struct gpio_sub g_subs[GPIO_MAX_SUBS]
	__attribute__((section(".user_bss")));
static struct port_slot g_ports[GPIO_PORT_SLOTS]
	__attribute__((section(".user_bss")));

static uintptr_t port_phys(uint8_t port)
{
	switch (port) {
	case 0u:  return PORT00_BASE;
	case 2u:  return PORT02_BASE;
	case 13u: return PORT13_BASE;
	case 14u: return PORT14_BASE;
	case 15u: return PORT15_BASE;
	case 20u: return PORT20_BASE;
	default:  return 0u;
	}
}

static void *map_port(uint8_t port)
{
	uint32_t i;
	uintptr_t phys;
	void *base;

	for (i = 0u; i < GPIO_PORT_SLOTS; i++) {
		if (g_ports[i].used && g_ports[i].port == port)
			return g_ports[i].base;
	}
	phys = port_phys(port);
	if (!phys)
		return NULL;
	base = ulmk_mem_map((void *)phys, PORT_MAP_SIZE,
			    ULMK_PERM_READ | ULMK_PERM_WRITE,
			    ULMK_MMAP_PERIPH);
	if (!base)
		return NULL;
	for (i = 0u; i < GPIO_PORT_SLOTS; i++) {
		if (!g_ports[i].used) {
			g_ports[i].port = port;
			g_ports[i].base = base;
			g_ports[i].used = 1;
			return base;
		}
	}
	return base;
}

static int do_config(uint16_t enc, uint32_t dir, uint32_t pull)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	void *base;
	uint8_t pc;

	base = map_port(port);
	if (!base)
		return ULMK_EINVAL;
	if (dir == BOARD_GPIO_DIR_OUT)
		pc = PORT_PC_OUT_PP;
	else if (pull == BOARD_GPIO_PULL_UP)
		pc = PORT_PC_IN_PU;
	else
		pc = PORT_PC_IN_FLOAT;
	port_set_iocr(base, pin, pc);
	return ULMK_OK;
}

static int do_set(uint16_t enc, int value)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	void *base = map_port(port);

	if (!base)
		return ULMK_EINVAL;
	port_write_pin(base, pin, value ? 1 : 0);
	return ULMK_OK;
}

static int do_get(uint16_t enc, int *value)
{
	uint8_t port = (uint8_t)(enc >> 8);
	uint8_t pin = (uint8_t)(enc & 0xFFu);
	void *base = map_port(port);

	if (!base || !value)
		return ULMK_EINVAL;
	*value = port_read_pin(base, pin);
	return ULMK_OK;
}

static int do_subscribe(uint16_t enc, uint32_t edge, ulmk_notif_t n,
			uint32_t bit)
{
	uint32_t i;
	int val;

	if (n == ULMK_NOTIF_INVALID)
		return ULMK_EINVAL;
	if (do_get(enc, &val) != ULMK_OK)
		return ULMK_EINVAL;
	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active) {
			g_subs[i].pin = enc;
			g_subs[i].edge = edge;
			g_subs[i].notif = n;
			g_subs[i].bit = bit;
			g_subs[i].last = val;
			g_subs[i].active = 1;
			return ULMK_OK;
		}
	}
	return ULMK_ENOSPC;
}

static void poll_subs(void)
{
	uint32_t i;
	int val;
	int rise;
	int fall;

	for (i = 0u; i < GPIO_MAX_SUBS; i++) {
		if (!g_subs[i].active)
			continue;
		if (do_get(g_subs[i].pin, &val) != ULMK_OK)
			continue;
		rise = (val && !g_subs[i].last);
		fall = (!val && g_subs[i].last);
		g_subs[i].last = val;
		if ((rise && (g_subs[i].edge & BOARD_GPIO_EVT_RISING)) ||
		    (fall && (g_subs[i].edge & BOARD_GPIO_EVT_FALLING)))
			ulmk_notif_signal(g_subs[i].notif,
					  1u << g_subs[i].bit);
	}
}

static void gpio_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	int val;
	int rc;

	(void)arg;
	/* Pre-map P00 for LEDs/button. */
	(void)map_port(0u);

	for (;;) {
		poll_subs();
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = 0u;
		reply.words[1] = 0u;
		switch (msg.label) {
		case GPIO_MSG_CONFIG:
			rc = do_config((uint16_t)msg.words[0], msg.words[1],
				       msg.words[2]);
			reply.words[0] = (uint32_t)(int32_t)rc;
			break;
		case GPIO_MSG_SET:
			rc = do_set((uint16_t)msg.words[0],
				    (int)msg.words[1]);
			reply.words[0] = (uint32_t)(int32_t)rc;
			break;
		case GPIO_MSG_GET:
			rc = do_get((uint16_t)msg.words[0], &val);
			reply.words[0] = (uint32_t)(int32_t)rc;
			reply.words[1] = (uint32_t)val;
			break;
		case GPIO_MSG_SUBSCRIBE:
			rc = do_subscribe((uint16_t)msg.words[0], msg.words[1],
					  (ulmk_notif_t)msg.words[2],
					  msg.words[3]);
			reply.words[0] = (uint32_t)(int32_t)rc;
			break;
		default:
			reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

static int call3(uint32_t label, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ulmk_msg_t msg;

	msg.label = label;
	msg.words[0] = a0;
	msg.words[1] = a1;
	msg.words[2] = a2;
	msg.words[3] = 0u;
	if (ulmk_ep_call(g_ep, &msg) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)msg.words[0];
}

int board_gpio_config(uint16_t pin, uint32_t dir, uint32_t pull)
{
	return call3(GPIO_MSG_CONFIG, pin, dir, pull);
}

int board_gpio_set(uint16_t pin, int value)
{
	return call3(GPIO_MSG_SET, pin, (uint32_t)value, 0u);
}

int board_gpio_get(uint16_t pin, int *value)
{
	ulmk_msg_t msg;
	int rc;

	msg.label = GPIO_MSG_GET;
	msg.words[0] = pin;
	if (ulmk_ep_call(g_ep, &msg) != ULMK_OK)
		return ULMK_EINVAL;
	rc = (int)(int32_t)msg.words[0];
	if (value && rc == ULMK_OK)
		*value = (int)msg.words[1];
	return rc;
}

int board_gpio_subscribe(uint16_t pin, uint32_t edge, ulmk_notif_t n,
			 uint32_t bit)
{
	ulmk_msg_t msg;

	msg.label = GPIO_MSG_SUBSCRIBE;
	msg.words[0] = pin;
	msg.words[1] = edge;
	msg.words[2] = (uint32_t)n;
	msg.words[3] = bit;
	if (ulmk_ep_call(g_ep, &msg) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)msg.words[0];
}

ulmk_tid_t board_gpio_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)info;
	g_ep = ulmk_ep_create();
	attr.name = "bgpio";
	attr.entry = gpio_server;
	attr.priority = 2u;
	attr.stack_size = 1536u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
