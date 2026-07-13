/* SPDX-License-Identifier: MIT */
/*
 * gpio client — public API wrappers (IPC hidden).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"

static int gpio_call(ulmk_msg_t *msg)
{
	if (g_gpio_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_gpio_ep, msg);
}

int gpio_config(uint16_t pin, uint32_t dir, uint32_t pull, uint32_t alt)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_CONFIG;
	msg.words[0] = pin;
	msg.words[1] = dir;
	msg.words[2] = pull;
	msg.words[3] = alt;
	rc = gpio_call(&msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int gpio_set(uint16_t pin, int value)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_SET;
	msg.words[0] = pin;
	msg.words[1] = (uint32_t)value;
	rc = gpio_call(&msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int gpio_get(uint16_t pin, int *value)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_GET;
	msg.words[0] = pin;
	rc = gpio_call(&msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	if (value)
		*value = (int)msg.words[1];
	return ULMK_OK;
}

int gpio_subscribe(uint16_t pin, uint32_t edge, ulmk_notif_t n, uint32_t bit)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_SUBSCRIBE;
	msg.words[0] = pin;
	msg.words[1] = edge;
	msg.words[2] = (uint32_t)n;
	msg.words[3] = bit;
	rc = gpio_call(&msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int gpio_irq_kick(void)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_IRQ_KICK;
	msg.words[0] = 0u;
	msg.words[1] = 0u;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	rc = gpio_call(&msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}
