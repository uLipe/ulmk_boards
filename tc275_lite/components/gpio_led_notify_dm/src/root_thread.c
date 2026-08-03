/* SPDX-License-Identifier: MIT */
/*
 * gpio_led_notify_dm — poll Button1 via /dev/gpio0, toggle LEDs.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk_device.h>
#include <ulmk_device_gpio.h>
#include <gpio_dm.h>
#include "board_config.h"
#include "board_console.h"
#include "board_devices.h"
#include "board_leds.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define GPIO_N		0u
#define BTN_PIN		((uint16_t)(((uint16_t)ULMK_BOARD_BUTTON_PORT << 8) | \
			 ((uint16_t)ULMK_BOARD_BUTTON_PIN)))
#define POLL_US		20000u

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	ulmk_dev_t gpio;
	int level;
	int prev;
	int led1_on;
	int rc;

	board_services_init(info);

	tid = gpio_dm_init(GPIO_N);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("gpio_dm_init failed\r\n");
		ulmk_thread_exit();
	}
	if (board_devices_register_gpio(GPIO_N, gpio_dm_ep(GPIO_N), tid)
	    != ULMK_OK) {
		board_console_puts("register /dev/gpio0 failed\r\n");
		ulmk_thread_exit();
	}

	if (ulmk_open("/dev/gpio0", &gpio) != ULMK_OK) {
		board_console_puts("ulmk_open(/dev/gpio0) failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("\r\n");
	board_console_puts(
		"ulmk: gpio_led_notify_dm - poll Button1, toggle LEDs\r\n");

	led1_on = 1;
	(void)board_leds_set(BOARD_LED_1, 1);
	(void)board_leds_set(BOARD_LED_2, 0);

	rc = ulmk_gpio_config(&gpio, BTN_PIN, ULMK_GPIO_DIR_IN,
			      ULMK_GPIO_PULL_UP);
	if (rc != ULMK_OK) {
		board_console_puts("gpio_config(button) failed\r\n");
		ulmk_thread_exit();
	}

	prev = 1;
	(void)ulmk_gpio_get(&gpio, BTN_PIN, &prev);

	board_console_puts("gpio_led_notify_dm: ready (press Button1)\r\n");

	for (;;) {
		board_timer_sleep_us(POLL_US);
		rc = ulmk_gpio_get(&gpio, BTN_PIN, &level);
		if (rc != ULMK_OK)
			continue;
		if (prev != 0 && level == 0) {
			led1_on = !led1_on;
			(void)board_leds_set(BOARD_LED_1, led1_on);
			(void)board_leds_set(BOARD_LED_2, !led1_on);
			if (led1_on)
				board_console_puts(
					"gpio_led_notify_dm: LED1 on LED2 off\r\n");
			else
				board_console_puts(
					"gpio_led_notify_dm: LED1 off LED2 on\r\n");
		}
		prev = level;
	}
}
