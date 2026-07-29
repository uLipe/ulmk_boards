/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"
#include "board_config.h"

#define GPIO_STACK_SIZE		1536u

ulmk_ep_t g_gpio_eps[GPIO_MAX];

static uint8_t pin_gpio(uint16_t enc)
{
	return (uint8_t)(enc & 0xFFu);
}

static void gpio_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	ulmk_ep_t ep = g_gpio_eps[(uint8_t)(uintptr_t)arg];
	int level;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		level = 0;
		switch (msg.label) {
		case GPIO_MSG_CONFIG:
			reply.words[0] = (uint32_t)gpio_hw_config(
				pin_gpio((uint16_t)msg.words[0]),
				msg.words[1], msg.words[2]);
			break;
		case GPIO_MSG_SET:
			reply.words[0] = (uint32_t)gpio_hw_set(
				pin_gpio((uint16_t)msg.words[0]),
				(int)msg.words[1]);
			break;
		case GPIO_MSG_GET:
			reply.words[0] = (uint32_t)gpio_hw_get(
				pin_gpio((uint16_t)msg.words[0]), &level);
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
	if (g_gpio_eps[n] != ULMK_EP_INVALID && g_gpio_eps[n] != 0)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	/* Publish before the thread runs: it resolves its ep from the table. */
	g_gpio_eps[n] = ep;

	attr.name       = "gpio";
	attr.entry      = gpio_server;
	attr.arg        = (void *)(uintptr_t)n;
	attr.priority   = 1u;
	attr.stack_size = GPIO_STACK_SIZE;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		g_gpio_eps[n] = ULMK_EP_INVALID;
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
