/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"
#include "board_leds.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	int on;

	board_services_init(info);
	(void)board_leds_init();
	board_console_puts("\r\n");
	board_console_puts("ulmk: ESP32-P4 Function EV blinky\r\n");
	board_console_puts("status LED = GPIO26 backlight pad, 100 ms\r\n");
	on = 1;
	for (;;) {
		(void)board_leds_set(BOARD_LED_1, on);
		board_console_puts(on ? "blinky LED on\r\n" : "blinky LED off\r\n");
		on = !on;
		board_timer_sleep_us(100000u);
	}
}
