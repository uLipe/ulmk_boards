/* SPDX-License-Identifier: MIT */
/*
 * board_blinky — toggle status LED (green / LD1) @ 100 ms.
 */
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

	board_console_puts("\r\n");
	board_console_puts("ulmk: Witte-Linum blinky (STM32H753ZI)\r\n");
	board_console_puts("status LED = green LD1 (GPIOG2), 100 ms\r\n");

	on = 1;
	for (;;) {
		(void)board_leds_set(BOARD_LED_1, on);
		on = !on;
		board_timer_sleep_us(100000u);
	}
}
