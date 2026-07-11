/* SPDX-License-Identifier: MIT */
#ifndef BOARD_LEDS_H
#define BOARD_LEDS_H

#include <ulmk/microkernel.h>
#include <stdint.h>

#define BOARD_LED_1	0u
#define BOARD_LED_2	1u
#define BOARD_LED_COUNT	2u

int board_leds_init(void);
int board_leds_set(uint32_t led, int on);
int board_leds_get(uint32_t led, int *on);
int board_leds_toggle(uint32_t led);

#endif /* BOARD_LEDS_H */
