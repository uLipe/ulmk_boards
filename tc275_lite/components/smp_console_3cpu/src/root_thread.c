/* SPDX-License-Identifier: MIT */
/*
 * smp_console_3cpu — smoke: one DRIVER thread pinned per core, say hello.
 *
 * Build: --enable-smp --component smp_console_3cpu
 * Expect: "on CPU0", "on CPU1", "on CPU2" in the UART capture.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include "board_config.h"
#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);

#define PERIOD_US	500000u

static ULMK_PRIVATE volatile uint32_t g_seen_mask;

static void worker(void *arg)
{
	uint32_t expect = (uint32_t)(uintptr_t)arg;
	uint32_t cpu;
	uint32_t seq = 0u;

	cpu = ulmk_cpu_id();
	g_seen_mask |= (1u << cpu);

	if (cpu != expect)
		board_console_printf(
			"smp_console_3cpu: hello on CPU%u (WARN affinity mismatch)\r\n",
			cpu);
	else
		board_console_printf("smp_console_3cpu: hello on CPU%u\r\n",
				     cpu);

	for (;;) {
		board_console_printf(
			"smp_console_3cpu: beat on CPU%u seq=%u\r\n",
			cpu, seq);
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;
	uint32_t cpu;

	board_services_init(info);
	g_seen_mask = 0u;

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_console_3cpu (one thread per core)\r\n");

	if (ulmk_cpu_id() != 0u) {
		board_console_puts(
			"smp_console_3cpu: FAIL root not on CPU0\r\n");
		ulmk_thread_exit();
	}

	if ((uint32_t)ULMK_ARCH_NUM_CPU < 3u) {
		board_console_puts("smp_console_3cpu: FAIL NUM_CPU < 3\r\n");
		ulmk_thread_exit();
	}

	for (cpu = 0u; cpu < 3u; cpu++) {
		attr.name       = "hello_cpu";
		attr.entry      = worker;
		attr.arg        = (void *)(uintptr_t)cpu;
		attr.priority   = 5u;
		attr.stack_size = 2048u;
		attr.privilege  = ULMK_PRIV_DRIVER;
		attr.heap_size  = 0u;
		attr.cpu        = (uint8_t)cpu;
		tid = ulmk_thread_create(&attr);
		if (tid == ULMK_TID_INVALID) {
			board_console_printf(
				"smp_console_3cpu: FAIL spawn CPU%u\r\n",
				cpu);
			ulmk_thread_exit();
		}
	}

	board_console_puts("smp_console_3cpu: workers spawned\r\n");
	ulmk_thread_exit();
}
