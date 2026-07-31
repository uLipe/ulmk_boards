/* SPDX-License-Identifier: MIT */
/*
 * smp_display_touch — display server affinity CPU0, touch server CPU1.
 *
 * Build: --enable-smp --component smp_display_touch
 * Expect: "smp_display_touch: ready" then optional "touch x=" lines.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <display.h>
#include <touch.h>
#include "board_config.h"
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

static ULMK_PRIVATE volatile uint32_t g_touch_cpu;
static ULMK_PRIVATE volatile uint32_t g_disp_ok;

static void touch_worker(void *arg)
{
	int x, y, pressed;
	uint32_t n = 0u;

	(void)arg;
	g_touch_cpu = ulmk_cpu_id() + 1u;
	board_console_printf("smp_display_touch: touch on CPU%u\r\n",
			     ulmk_cpu_id());

	for (;;) {
		(void)touch_read(&x, &y, &pressed);
		if (pressed) {
			board_console_printf(
				"smp_display_touch: touch x=%d y=%d\r\n",
				x, y);
			n++;
		}
		board_timer_sleep_us(200000u);
		(void)n;
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;
	ulmk_tid_t disp;

	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_display_touch\r\n");

	if (ulmk_cpu_id() != 0u) {
		board_console_puts(
			"smp_display_touch: FAIL root not on CPU0\r\n");
		ulmk_thread_exit();
	}
	if ((uint32_t)ULMK_ARCH_NUM_CPU < 2u) {
		board_console_puts(
			"smp_display_touch: FAIL NUM_CPU < 2\r\n");
		ulmk_thread_exit();
	}

	/*
	 * display_init() is root-only and spawns the server on the caller's
	 * CPU (0).  Touch init similarly; then pin a reader on CPU1.
	 */
	disp = display_init(0u);
	if (disp == ULMK_TID_INVALID) {
		board_console_puts("smp_display_touch: FAIL display_init\r\n");
		ulmk_thread_exit();
	}
	g_disp_ok = 1u;
	board_console_puts("smp_display_touch: display on CPU0\r\n");

	if (touch_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("smp_display_touch: FAIL touch_init\r\n");
		ulmk_thread_exit();
	}

	attr.name       = "touch_rd";
	attr.entry      = touch_worker;
	attr.arg        = NULL;
	attr.priority   = 5u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 1u;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_display_touch: FAIL spawn touch\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("smp_display_touch: ready\r\n");
	ulmk_thread_exit();
}
