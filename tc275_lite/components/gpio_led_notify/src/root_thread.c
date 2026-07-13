/* SPDX-License-Identifier: MIT */
/*
 * gpio_led_notify — poll Button1 (P00.7) and toggle LED1/LED2.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <gpio.h>
#include "board_config.h"
#include "board_leds.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_timer_sleep_us(uint32_t us);

#define GPIO_N		0u
#define BTN_PIN		GPIO_PIN(ULMK_BOARD_BUTTON_PORT, ULMK_BOARD_BUTTON_PIN)
#define POLL_US		20000u

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	int level;
	int prev;
	int led1_on;
	int rc;

	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: gpio_led_notify - poll Button1, toggle LEDs\r\n");

	led1_on = 1;
	(void)board_leds_set(BOARD_LED_1, 1);
	(void)board_leds_set(BOARD_LED_2, 0);

	rc = gpio_config(GPIO_N, BTN_PIN, GPIO_DIR_IN, GPIO_PULL_UP);
	if (rc != ULMK_OK) {
		board_console_puts("gpio_config(button) failed\r\n");
		ulmk_thread_exit();
	}

	prev = 1;
	(void)gpio_get(GPIO_N, BTN_PIN, &prev);

	board_console_puts("gpio_led_notify: ready (press Button1)\r\n");

	for (;;) {
		board_timer_sleep_us(POLL_US);
		rc = gpio_get(GPIO_N, BTN_PIN, &level);
		if (rc != ULMK_OK)
			continue;
		if (prev != 0 && level == 0) {
			led1_on = !led1_on;
			(void)board_leds_set(BOARD_LED_1, led1_on);
			(void)board_leds_set(BOARD_LED_2, !led1_on);
			if (led1_on)
				board_console_puts(
					"gpio_led_notify: LED1 on LED2 off\r\n");
			else
				board_console_puts(
					"gpio_led_notify: LED1 off LED2 on\r\n");
		}
		prev = level;
	}
}
