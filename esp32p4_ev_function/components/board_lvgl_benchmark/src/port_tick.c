/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <lvgl.h>
#include "board_timer.h"
#include "port_tick.h"

static uint32_t tick_get_cb(void)
{
	return board_timer_now_ms();
}

void port_tick_init(void)
{
	lv_tick_set_cb(tick_get_cb);
}

uint32_t ulmk_lvgl_idle_percent(void)
{
	/*
	 * Kernel has no CPU-load accounting yet.  Stub keeps sysmon happy;
	 * FPS / render / flush / mem monitors still work.
	 */
	return 100u;
}
