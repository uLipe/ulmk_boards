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

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_console_putc(char c);
void board_timer_sleep_us(uint32_t us);

#define PERIOD_US	500000u

static ULMK_PRIVATE volatile uint32_t g_print_lock;
static ULMK_PRIVATE volatile uint32_t g_seen_mask;

static void print_lock(void)
{
	while (g_print_lock != 0u)
		ulmk_thread_yield();
	g_print_lock = 1u;
	__asm__ volatile("" ::: "memory");
}

static void print_unlock(void)
{
	__asm__ volatile("" ::: "memory");
	g_print_lock = 0u;
}

static void put_u32(uint32_t v)
{
	char buf[10];
	uint32_t i = 0u;
	uint32_t n = v;

	if (n == 0u) {
		board_console_putc('0');
		return;
	}
	while (n > 0u && i < sizeof(buf)) {
		buf[i++] = (char)('0' + (n % 10u));
		n /= 10u;
	}
	while (i > 0u)
		board_console_putc(buf[--i]);
}

static void worker(void *arg)
{
	uint32_t expect = (uint32_t)(uintptr_t)arg;
	uint32_t cpu;
	uint32_t seq = 0u;

	cpu = ulmk_cpu_id();
	g_seen_mask |= (1u << cpu);

	print_lock();
	board_console_puts("smp_console_3cpu: hello on CPU");
	put_u32(cpu);
	if (cpu != expect)
		board_console_puts(" (WARN affinity mismatch)");
	board_console_puts("\r\n");
	print_unlock();

	for (;;) {
		print_lock();
		board_console_puts("smp_console_3cpu: beat on CPU");
		put_u32(cpu);
		board_console_puts(" seq=");
		put_u32(seq);
		board_console_puts("\r\n");
		print_unlock();
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
	g_print_lock = 0u;
	g_seen_mask = 0u;

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_console_3cpu (one thread per core)\r\n");

	if (ulmk_cpu_id() != 0u) {
		board_console_puts("smp_console_3cpu: FAIL root not on CPU0\r\n");
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
			board_console_puts("smp_console_3cpu: FAIL spawn CPU");
			put_u32(cpu);
			board_console_puts("\r\n");
			ulmk_thread_exit();
		}
	}

	board_console_puts("smp_console_3cpu: workers spawned\r\n");
	ulmk_thread_exit();
}
