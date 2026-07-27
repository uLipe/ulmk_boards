/* SPDX-License-Identifier: MIT */
#ifndef GPIO_INTERNAL_H
#define GPIO_INTERNAL_H

#include <ulmk/microkernel.h>
#include <gpio.h>

#define GPIO_MSG_CONFIG		1u
#define GPIO_MSG_SET		2u
#define GPIO_MSG_GET		3u
#define GPIO_MSG_SUBSCRIBE	4u

extern ulmk_ep_t g_gpio_eps[GPIO_MAX];

#endif /* GPIO_INTERNAL_H */
