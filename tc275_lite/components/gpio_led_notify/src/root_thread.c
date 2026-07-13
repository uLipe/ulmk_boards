/* SPDX-License-Identifier: MIT */
/*
 * gpio_led_notify — turn LED1 on when Button1 (P00.7) is pressed.
 *
 * Demonstrates IRQ-driven gpio_subscribe(): register a notification, return
 * immediately, then wait for the deferred signal from the GPIO IRQ server.
 *
 * A gpio_irq_kick() after ready exercises the same defer path as the TIM IRQ
 * (automated HIL).  Further events come from Button1 via GTM TIM2 CH6.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <gpio.h>
#include "board_config.h"
#include "board_leds.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);

#define BTN_PIN	GPIO_PIN(ULMK_BOARD_BUTTON_PORT, ULMK_BOARD_BUTTON_PIN)
#define BTN_BIT	0u

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_notif_t n;
	uint32_t bits;
	int rc;

	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: gpio_led_notify - Button1 -> LED1\r\n");

	(void)board_leds_set(BOARD_LED_1, 0);
	(void)board_leds_set(BOARD_LED_2, 0);

	rc = gpio_config(BTN_PIN, GPIO_DIR_IN, GPIO_PULL_UP, GPIO_ALT_GENERAL);
	if (rc != ULMK_OK) {
		board_console_puts("gpio_config(button) failed\r\n");
		ulmk_thread_exit();
	}

	n = ulmk_notif_create();
	if (n == ULMK_NOTIF_INVALID) {
		board_console_puts("notif_create failed\r\n");
		ulmk_thread_exit();
	}

	rc = gpio_subscribe(BTN_PIN, GPIO_EVT_FALLING, n, BTN_BIT);
	if (rc != ULMK_OK) {
		board_console_puts("gpio_subscribe failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("gpio_led_notify: ready (press Button1)\r\n");

	/* Same defer path the TIM IRQ uses — validates subscribe without SETR. */
	rc = gpio_irq_kick();
	if (rc != ULMK_OK) {
		board_console_puts("gpio_irq_kick failed\r\n");
		ulmk_thread_exit();
	}

	for (;;) {
		bits = 0u;
		rc = ulmk_notif_wait(n, 1u << BTN_BIT, &bits);
		if (rc != ULMK_OK)
			continue;
		(void)board_leds_set(BOARD_LED_1, 1);
		board_console_puts("gpio_led_notify: LED1 on\r\n");
	}
}
