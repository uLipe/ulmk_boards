/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_leds.h"
#include <gpio.h>

#define GPIO_N	0u

int board_leds_init(void)
{
	uint16_t pin = GPIO_PIN(0, ULMK_BOARD_LED1_GPIO);
	int rc;

	/*
	 * LED1 aliases the backlight PWM pad.  Only mux as GPIO here —
	 * do not force level 0 (that fights display BL / LEDC).
	 */
	rc = gpio_config(GPIO_N, pin, GPIO_DIR_OUT, GPIO_PULL_NONE);
	if (rc != ULMK_OK)
		return rc;
	return ULMK_OK;
}

int board_leds_set(uint32_t led, int on)
{
	if (led >= BOARD_LED_COUNT)
		return ULMK_EINVAL;
	return gpio_set(GPIO_N, GPIO_PIN(0, ULMK_BOARD_LED1_GPIO), on ? 1 : 0);
}

int board_leds_get(uint32_t led, int *on)
{
	int level;
	int rc;

	if (led >= BOARD_LED_COUNT || !on)
		return ULMK_EINVAL;
	rc = gpio_get(GPIO_N, GPIO_PIN(0, ULMK_BOARD_LED1_GPIO), &level);
	if (rc != ULMK_OK)
		return rc;
	*on = level ? 1 : 0;
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
