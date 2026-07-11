/* SPDX-License-Identifier: MIT */
#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <ulmk/microkernel.h>
#include <stdint.h>

/* Encoded pin: (port_num << 8) | pin — port_num 0 = P00. */
#define BOARD_GPIO_PIN(port, pin) \
	((uint16_t)((((uint16_t)(port)) << 8) | ((uint16_t)(pin) & 0xFFu)))

#define BOARD_GPIO_DIR_IN	0u
#define BOARD_GPIO_DIR_OUT	1u

#define BOARD_GPIO_PULL_NONE	0u
#define BOARD_GPIO_PULL_UP	1u

#define BOARD_GPIO_EVT_NONE	0u
#define BOARD_GPIO_EVT_FALLING	1u
#define BOARD_GPIO_EVT_RISING	2u
#define BOARD_GPIO_EVT_BOTH	3u

ulmk_tid_t board_gpio_start(const ulmk_boot_info_t *info);

int board_gpio_config(uint16_t pin, uint32_t dir, uint32_t pull);
int board_gpio_set(uint16_t pin, int value);
int board_gpio_get(uint16_t pin, int *value);
int board_gpio_subscribe(uint16_t pin, uint32_t edge, ulmk_notif_t n,
			 uint32_t bit);

#endif /* BOARD_GPIO_H */
