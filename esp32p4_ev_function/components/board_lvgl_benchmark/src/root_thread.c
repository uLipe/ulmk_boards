/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <display.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t frames;
	uint32_t i;

	board_services_init(info);
	if (display_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("LVGL: display init failed\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("LVGL stub (soft FB flips — not real LVGL)\r\n");
	frames = 0u;
	for (i = 0u; i < 30u; i++) {
		(void)display_flip();
		frames++;
		board_timer_sleep_us(33000u); /* ~30 FPS cadence */
	}
	board_console_printf("LVGL FPS approx=%u\r\n",
			     (unsigned)(frames * 1000u / 990u));
	for (;;)
		board_timer_sleep_us(1000000u);
}
