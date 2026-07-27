/* SPDX-License-Identifier: MIT */
/*
 * pinmux server — owns GPIO port mem_map and pad mode (STM32 LL).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "pinmux_internal.h"
#include "board_config.h"

#define PINMUX_STACK		1024u
#define PINMUX_PORT_SLOTS	11u

struct port_slot {
	uint8_t       port;
	GPIO_TypeDef *gpio;
	int           used;
};

ulmk_ep_t g_pinmux_eps[ULMK_BOARD_PINMUX_MAX];
static struct port_slot g_ports[PINMUX_PORT_SLOTS]
	__attribute__((section(".user_bss")));

static const uint8_t g_board_ports[] = {
	0u, 1u, 4u, 6u, 7u, 8u, 9u, 10u,
};

static uintptr_t gpio_phys(uint8_t port)
{
	return (uintptr_t)ULMK_BOARD_GPIOA_BASE +
	       (uintptr_t)port * (uintptr_t)ULMK_BOARD_GPIO_STRIDE;
}

static uint32_t pin_mask(uint8_t pin)
{
	return 1u << (pin & 15u);
}

static GPIO_TypeDef *map_port_once(uint8_t port)
{
	uint32_t i;
	void *mapped;

	if (port > 10u)
		return NULL;

	for (i = 0u; i < PINMUX_PORT_SLOTS; i++) {
		if (g_ports[i].used && g_ports[i].port == port)
			return g_ports[i].gpio;
	}

	mapped = ulmk_mem_map((void *)gpio_phys(port), ULMK_BOARD_GPIO_STRIDE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		return NULL;

	for (i = 0u; i < PINMUX_PORT_SLOTS; i++) {
		if (!g_ports[i].used) {
			g_ports[i].port = port;
			g_ports[i].gpio = (GPIO_TypeDef *)mapped;
			g_ports[i].used = 1;
			return g_ports[i].gpio;
		}
	}
	return (GPIO_TypeDef *)mapped;
}

GPIO_TypeDef *pinmux_gpio(uint8_t port)
{
	return map_port_once(port);
}

int pinmux_apply(const pinmux_cfg_t *cfg)
{
	GPIO_TypeDef *gpio;
	uint32_t pin;
	uint32_t pull;
	uint32_t ot;

	if (!cfg || cfg->pin > 15u)
		return ULMK_EINVAL;
	gpio = map_port_once(cfg->port);
	if (!gpio)
		return ULMK_EINVAL;

	pin = pin_mask(cfg->pin);
	pull = LL_GPIO_PULL_NO;
	if (cfg->pull == PINMUX_PULL_UP)
		pull = LL_GPIO_PULL_UP;
	else if (cfg->pull == PINMUX_PULL_DOWN)
		pull = LL_GPIO_PULL_DOWN;

	ot = (cfg->flags & PINMUX_F_OPENDRAIN) ?
		LL_GPIO_OUTPUT_OPENDRAIN : LL_GPIO_OUTPUT_PUSHPULL;

	if (cfg->alt != PINMUX_ALT_GPIO) {
		LL_GPIO_SetPinMode(gpio, pin, LL_GPIO_MODE_ALTERNATE);
		LL_GPIO_SetPinSpeed(gpio, pin, LL_GPIO_SPEED_FREQ_HIGH);
		LL_GPIO_SetPinPull(gpio, pin, pull);
		LL_GPIO_SetPinOutputType(gpio, pin, ot);
		if (cfg->pin < 8u)
			LL_GPIO_SetAFPin_0_7(gpio, pin, cfg->alt);
		else
			LL_GPIO_SetAFPin_8_15(gpio, pin, cfg->alt);
		return ULMK_OK;
	}

	if (cfg->dir == PINMUX_DIR_IN) {
		LL_GPIO_SetPinMode(gpio, pin, LL_GPIO_MODE_INPUT);
		LL_GPIO_SetPinPull(gpio, pin, pull);
	} else {
		LL_GPIO_SetPinMode(gpio, pin, LL_GPIO_MODE_OUTPUT);
		LL_GPIO_SetPinOutputType(gpio, pin, ot);
		LL_GPIO_SetPinSpeed(gpio, pin, LL_GPIO_SPEED_FREQ_HIGH);
		LL_GPIO_SetPinPull(gpio, pin, pull);
	}
	return ULMK_OK;
}

static void pinmux_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	pinmux_cfg_t cfg;

	(void)arg;
	for (;;) {
		if (ulmk_ep_recv(g_pinmux_eps[0], &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		if (msg.label == PINMUX_MSG_CONFIG) {
			cfg.port  = (uint8_t)(msg.words[0] >> 24);
			cfg.pin   = (uint8_t)(msg.words[0] >> 16);
			cfg.dir   = (uint8_t)(msg.words[0] >> 8);
			cfg.pull  = (uint8_t)msg.words[0];
			cfg.alt   = (uint8_t)(msg.words[1] >> 8);
			cfg.flags = (uint8_t)msg.words[1];
			reply.words[0] = (uint32_t)pinmux_apply(&cfg);
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t pinmux_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	uint32_t i;

	if (n >= ULMK_BOARD_PINMUX_MAX)
		return ULMK_TID_INVALID;
	if (g_pinmux_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name       = "pinmux";
	attr.entry      = pinmux_server;
	attr.priority   = 1u;
	attr.stack_size = PINMUX_STACK;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	g_pinmux_eps[n] = ep;

	for (i = 0u; i < sizeof(g_board_ports); i++)
		(void)map_port_once(g_board_ports[i]);

	return tid;
}
