/* SPDX-License-Identifier: MIT */
/*
 * smp_affinity_console — one DRIVER thread pinned per core, say hello.
 *
 * Build: --enable-smp --component smp_affinity_console
 * Expect: "on CPU0" and "on CPU1" in the UART capture.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

#define PERIOD_US	500000u

static void worker(void *arg)
{
	uint32_t expect = (uint32_t)(uintptr_t)arg;
	uint32_t cpu;
	uint32_t seq = 0u;

	cpu = ulmk_cpu_id();

	if (cpu != expect)
		board_console_printf(
			"smp_affinity_console: hello on CPU%u (WARN affinity)\r\n",
			cpu);
	else
		board_console_printf("smp_affinity_console: hello on CPU%u\r\n",
				     cpu);

	for (;;) {
		board_console_printf(
			"smp_affinity_console: beat on CPU%u seq=%u\r\n",
			cpu, seq);
		seq++;
		board_timer_sleep_us(PERIOD_US);
	}
}

static ulmk_tid_t spawn_cpu(uint32_t cpu)
{
	ulmk_thread_attr_t attr = {0};

	attr.name       = "hello_cpu";
	attr.entry      = worker;
	attr.arg        = (void *)(uintptr_t)cpu;
	attr.priority   = 5u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = (uint8_t)cpu;
	return ulmk_thread_create(&attr);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_affinity_console (one thread per core)\r\n");

	if (ulmk_cpu_id() != 0u) {
		board_console_puts(
			"smp_affinity_console: FAIL root not on CPU0\r\n");
		ulmk_thread_exit();
	}

	if ((uint32_t)ULMK_ARCH_NUM_CPU < 2u) {
		board_console_puts(
			"smp_affinity_console: FAIL NUM_CPU < 2\r\n");
		ulmk_thread_exit();
	}

	if (spawn_cpu(0u) == ULMK_TID_INVALID ||
	    spawn_cpu(1u) == ULMK_TID_INVALID) {
		board_console_puts("smp_affinity_console: FAIL spawn\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("smp_affinity_console: workers spawned\r\n");
	ulmk_thread_exit();
}
