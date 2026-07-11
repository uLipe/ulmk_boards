/* SPDX-License-Identifier: MIT */
/*
 * board_leds.c — Lite Kit LED1/LED2 (P00.5 / P00.6, active-low) via GPIO.
 */

#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_gpio.h"
#include "board_leds.h"

static const uint16_t g_led_pin[BOARD_LED_COUNT] = {
	BOARD_GPIO_PIN(0, 5),	/* LED1 P00.5 */
	BOARD_GPIO_PIN(0, 6),	/* LED2 P00.6 */
};

int board_leds_init(void)
{
	uint32_t i;
	int rc;

	for (i = 0u; i < BOARD_LED_COUNT; i++) {
		rc = board_gpio_config(g_led_pin[i], BOARD_GPIO_DIR_OUT,
				       BOARD_GPIO_PULL_NONE);
		if (rc != ULMK_OK)
			return rc;
		/* Off = high (active-low). */
		rc = board_gpio_set(g_led_pin[i], 1);
		if (rc != ULMK_OK)
			return rc;
	}
	return ULMK_OK;
}

int board_leds_set(uint32_t led, int on)
{
	if (led >= BOARD_LED_COUNT)
		return ULMK_EINVAL;
	/* Active-low: on → drive 0. */
	return board_gpio_set(g_led_pin[led], on ? 0 : 1);
}

int board_leds_get(uint32_t led, int *on)
{
	int level;
	int rc;

	if (led >= BOARD_LED_COUNT || !on)
		return ULMK_EINVAL;
	rc = board_gpio_get(g_led_pin[led], &level);
	if (rc != ULMK_OK)
		return rc;
	*on = level ? 0 : 1;
	return ULMK_OK;
}

int board_leds_toggle(uint32_t led)
{
	int on;
	int rc;

	rc = board_leds_get(led, &on);
	if (rc != ULMK_OK)
		return rc;
	return board_leds_set(led, !on);
}
