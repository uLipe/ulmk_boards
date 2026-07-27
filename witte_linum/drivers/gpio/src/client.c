/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "gpio_internal.h"

static int gpio_call(uint8_t n, ulmk_msg_t *msg)
{
	if (n >= GPIO_MAX || g_gpio_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_gpio_eps[n], msg);
}

int gpio_config(uint8_t n, uint16_t pin, uint32_t dir, uint32_t pull)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_CONFIG;
	msg.words[0] = pin;
	msg.words[1] = dir;
	msg.words[2] = pull;
	msg.words[3] = 0u;
	rc = gpio_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int gpio_set(uint8_t n, uint16_t pin, int value)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_SET;
	msg.words[0] = pin;
	msg.words[1] = (uint32_t)value;
	rc = gpio_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int gpio_get(uint8_t n, uint16_t pin, int *value)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_GET;
	msg.words[0] = pin;
	rc = gpio_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	if (value)
		*value = (int)msg.words[1];
	return ULMK_OK;
}

int gpio_subscribe(uint8_t n, uint16_t pin, uint32_t edge,
		   ulmk_notif_t notif, uint32_t bit)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = GPIO_MSG_SUBSCRIBE;
	msg.words[0] = pin;
	msg.words[1] = edge;
	msg.words[2] = (uint32_t)notif;
	msg.words[3] = bit;
	rc = gpio_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}
