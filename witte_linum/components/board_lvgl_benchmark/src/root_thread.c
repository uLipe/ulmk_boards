/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <display.h>
#include <pwm.h>
#include <i2c.h>
#include <touch.h>
#include "board_console.h"
#include "board_services.h"
#include "board_config.h"
#include "app_benchmark.h"

/*
 * Kernel root stack is 4 KiB.  Scrolling / opa_layered SW blend need a deep
 * stack — run the demo on a dedicated DRIVER thread with a slab stack.
 */
#define LVGL_BENCH_STACK	(128u * 1024u)

static void lvgl_bench_thread(void *arg)
{
	(void)arg;
	board_console_puts("lvgl_bench running\r\n");
	app_benchmark_run();
	ulmk_thread_exit();
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;

	board_services_init(info);
	board_console_puts("\r\nlvgl benchmark boot\r\n");

	if (display_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("display init failed\r\n");
		ulmk_thread_exit();
	}
	(void)display_on(1);

	if (pwm_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("pwm init failed (cont)\r\n");
	} else {
		(void)pwm_config(ULMK_BOARD_PWM_BACKLIGHT, 1000u, 900u);
		(void)pwm_enable(ULMK_BOARD_PWM_BACKLIGHT, 1);
	}

	if (i2c_init(0u, ULMK_BOARD_I2C_BITRATE_HZ) == ULMK_TID_INVALID) {
		board_console_puts("i2c init failed\r\n");
		ulmk_thread_exit();
	}
	if (touch_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("touch init failed\r\n");
		ulmk_thread_exit();
	}

	attr = (ulmk_thread_attr_t){
		.name = "lvgl_bench",
		.entry = lvgl_bench_thread,
		.arg = NULL,
		.priority = 10u,
		.stack_size = LVGL_BENCH_STACK,
		.privilege = ULMK_PRIV_DRIVER,
		.heap_size = 0u,
		.cpu = 0u,
	};
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("lvgl_bench spawn failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_cap_grant(tid, ULMK_CAP_MAP_SHARED | ULMK_CAP_MAP_PERIPH |
				 ULMK_CAP_IRQ) != ULMK_OK) {
		board_console_puts("lvgl_bench cap grant failed\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("lvgl_bench spawned\r\n");
	ulmk_thread_exit();
}
