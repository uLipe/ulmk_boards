/* SPDX-License-Identifier: MIT */
#ifndef BOARD_LEDS_H
#define BOARD_LEDS_H

#include <stdint.h>

#define BOARD_LED_1	0u	/* green / status */
#define BOARD_LED_2	1u	/* red */
#define BOARD_LED_3	2u	/* blue */
#define BOARD_LED_COUNT	3u

int board_leds_init(void);
int board_leds_set(uint32_t led, int on);
int board_leds_get(uint32_t led, int *on);
int board_leds_toggle(uint32_t led);

#endif /* BOARD_LEDS_H */
