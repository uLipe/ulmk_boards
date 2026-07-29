/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <touch.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	int x, y, pressed;

	board_services_init(info);
	(void)touch_init(0u);
	board_console_puts("display touch running\r\n");
	board_console_puts("touch waiting\r\n");
	for (;;) {
		(void)touch_read(&x, &y, &pressed);
		if (pressed)
			board_console_printf("touch x=%d y=%d\r\n", x, y);
		board_timer_sleep_us(200000u);
	}
}
