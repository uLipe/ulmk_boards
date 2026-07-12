/* SPDX-License-Identifier: MIT */
/*
 * board_leds.c — Lite Kit LED1/LED2 (P00.5 / P00.6, active-low) via gpio driver.
 */
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_leds.h"
#include <gpio.h>

static const uint16_t g_led_pin[BOARD_LED_COUNT] = {
	GPIO_PIN(ULMK_BOARD_LED1_PORT, ULMK_BOARD_LED1_PIN),
	GPIO_PIN(ULMK_BOARD_LED2_PORT, ULMK_BOARD_LED2_PIN),
};

int board_leds_init(void)
{
	uint32_t i;
	int rc;

	for (i = 0u; i < BOARD_LED_COUNT; i++) {
		rc = gpio_config(g_led_pin[i], GPIO_DIR_OUT, GPIO_PULL_NONE,
				 GPIO_ALT_GENERAL);
		if (rc != ULMK_OK)
			return rc;
		rc = gpio_set(g_led_pin[i], 1);
		if (rc != ULMK_OK)
			return rc;
	}
	return ULMK_OK;
}

int board_leds_set(uint32_t led, int on)
{
	if (led >= BOARD_LED_COUNT)
		return ULMK_EINVAL;
	return gpio_set(g_led_pin[led], on ? 0 : 1);
}

int board_leds_get(uint32_t led, int *on)
{
	int level;
	int rc;

	if (led >= BOARD_LED_COUNT || !on)
		return ULMK_EINVAL;
	rc = gpio_get(g_led_pin[led], &level);
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
