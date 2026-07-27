/* SPDX-License-Identifier: MIT */
/*
 * gpio server — digital I/O via pinmux-owned GPIO maps (STM32 LL).
 * EXTI subscribe is not implemented yet (ENOTSUP).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"

#define GPIO_STACK_SIZE		1536u

ulmk_ep_t g_gpio_eps[GPIO_MAX];

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
	GPIO_TypeDef *gpio;
	uint32_t pin;

	gpio = pinmux_gpio((uint8_t)(enc >> 8));
	if (!gpio)
		return ULMK_EINVAL;
	pin = 1u << ((enc & 0xFFu) & 15u);
	if (value)
		LL_GPIO_SetOutputPin(gpio, pin);
	else
		LL_GPIO_ResetOutputPin(gpio, pin);
	return ULMK_OK;
}

static int do_get(uint16_t enc, int *value)
{
	GPIO_TypeDef *gpio;
	uint32_t pin;

	gpio = pinmux_gpio((uint8_t)(enc >> 8));
	if (!gpio || !value)
		return ULMK_EINVAL;
	pin = 1u << ((enc & 0xFFu) & 15u);
	*value = LL_GPIO_IsInputPinSet(gpio, pin) ? 1 : 0;
	return ULMK_OK;
}

static void gpio_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	int level;

	(void)arg;
	for (;;) {
		if (ulmk_ep_recv(g_gpio_eps[0], &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		level = 0;
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
			reply.words[0] = (uint32_t)do_get(
				(uint16_t)msg.words[0], &level);
			reply.words[1] = (uint32_t)level;
			break;
		case GPIO_MSG_SUBSCRIBE:
			reply.words[0] = (uint32_t)ULMK_ENOTSUP;
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
	ulmk_ep_t ep;
	ulmk_tid_t tid;

	if (n >= GPIO_MAX)
		return ULMK_TID_INVALID;
	if (g_gpio_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name       = "gpio";
	attr.entry      = gpio_server;
	attr.priority   = 1u;
	attr.stack_size = GPIO_STACK_SIZE;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	g_gpio_eps[n] = ep;
	return tid;
}
