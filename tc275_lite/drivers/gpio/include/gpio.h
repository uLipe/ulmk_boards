/* SPDX-License-Identifier: MIT */
#ifndef GPIO_H
#define GPIO_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include "board_config.h"

/* Encoded pin: (port_num << 8) | pin — port_num 0 = P00. */
#define GPIO_PIN(port, pin) \
	((uint16_t)((((uint16_t)(port)) << 8) | ((uint16_t)(pin) & 0xFFu)))

#define GPIO_MAX		ULMK_BOARD_GPIO_MAX

#define GPIO_DIR_IN		0u
#define GPIO_DIR_OUT		1u

#define GPIO_PULL_NONE		0u
#define GPIO_PULL_UP		1u
#define GPIO_PULL_DOWN		2u

#define GPIO_EVT_NONE		0u
#define GPIO_EVT_FALLING	1u
#define GPIO_EVT_RISING		2u
#define GPIO_EVT_BOTH		3u

/*
 * gpio_init — start GPIO IPC + ERU IRQ servers for controller @p n.
 * Requires pinmux_init() first (PORTs mapped once there).
 */
ulmk_tid_t gpio_init(uint8_t n);

int gpio_config(uint8_t n, uint16_t pin, uint32_t dir, uint32_t pull);
int gpio_set(uint8_t n, uint16_t pin, int value);
int gpio_get(uint8_t n, uint16_t pin, int *value);

/*
 * gpio_subscribe — ERU edge notify (non-blocking register).
 * Only pins with an SCU ERU REQ mux; Button1 (P00.7) has none — poll it.
 */
int gpio_subscribe(uint8_t n, uint16_t pin, uint32_t edge,
		   ulmk_notif_t notif, uint32_t bit);

#endif /* GPIO_H */
