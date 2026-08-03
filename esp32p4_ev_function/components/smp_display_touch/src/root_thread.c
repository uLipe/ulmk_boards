/* SPDX-License-Identifier: MIT */
/*
 * smp_display_touch — display+touch via board_devices_register; reader on CPU1.
 *
 * Build: --enable-smp --component smp_display_touch
 * Expect: "smp_display_touch: ready" then optional "touch x=" lines.
 */
#include <stdint.h>
#include <string.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk_device.h>
#include <ulmk_device_input.h>
#include "board_config.h"
#include "board_console.h"
#include "board_services.h"
#include "board_devices.h"
#include "board_timer.h"

static ULMK_PRIVATE volatile uint32_t g_touch_cpu;

static void touch_worker(void *arg)
{
	ulmk_dev_t indev;
	struct ulmk_input_event ev;
	int n;

	(void)arg;
	memset(&indev, 0, sizeof(indev));
	g_touch_cpu = ulmk_cpu_id() + 1u;
	board_console_printf("smp_display_touch: touch on CPU%u\r\n",
			     ulmk_cpu_id());
	if (ulmk_open("/dev/input0", &indev) != ULMK_OK) {
		board_console_puts("smp_display_touch: open input failed\r\n");
		ulmk_thread_exit();
	}

	for (;;) {
		n = ulmk_read(&indev, &ev, sizeof(ev));
		if (n == (int)sizeof(ev) &&
		    ev.state == ULMK_INPUT_STATE_PRESSED) {
			board_console_printf(
				"smp_display_touch: touch x=%d y=%d\r\n",
				(int)ev.x, (int)ev.y);
		}
		board_timer_sleep_us(200000u);
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;

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

	if (board_devices_register() != ULMK_OK) {
		board_console_puts(
			"smp_display_touch: FAIL board_devices_register\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("smp_display_touch: display on CPU0\r\n");

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
