/* SPDX-License-Identifier: MIT */
/*
 * board_blinky — alternate LED4/LED5 @ 100 ms via ulmk_sleep_ms.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_timer_sleep_us(uint32_t us);
void board_led_set(uint32_t led, int on);
void board_led_toggle(uint32_t led);

#define BOARD_LED_1	0u
#define BOARD_LED_2	1u
#define PERIOD_US	100000u

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	unsigned n;

	board_services_init(info);

	board_console_puts("ulmk: board_blinky on LAUNCHXL-F29H85X\n");
	board_console_puts("ulmk: blinky LED4/LED5 active-low @ 100ms\n");
	board_console_puts("ULMK-HIL:c29_blinky:START\n");
	board_console_puts("cmds: help status (auto-blink; visual check LED4/LED5)\n");

	board_led_set(BOARD_LED_1, 1);
	board_led_set(BOARD_LED_2, 0);

	for (n = 0u; n < 20u; n++) {
		board_led_toggle(BOARD_LED_1);
		board_led_toggle(BOARD_LED_2);
		board_console_puts("ulmk: blinky alive\n");
		board_timer_sleep_us(PERIOD_US);
	}

	board_console_puts("ULMK-HIL:c29_blinky:PASS\n");
	board_console_puts("C29BLINKY_PASS\n");

	for (;;) {
		board_led_toggle(BOARD_LED_1);
		board_led_toggle(BOARD_LED_2);
		board_console_puts("ulmk: blinky alive\n");
		board_timer_sleep_us(PERIOD_US);
	}
}
