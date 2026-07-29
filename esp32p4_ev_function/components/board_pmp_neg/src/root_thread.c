/* SPDX-License-Identifier: MIT */
/*
 * PMP negative smoke — U-mode load outside HP peri window must trap and
 * kill the faulter; root continues and prints PMP_NEG: PASS.
 *
 * Note this only proves that an address covered by no grant faults.  It says
 * nothing about isolation between threads or from the kernel, which the
 * board does not have yet: the boot PMP entry that PRESERVE_BOOT keeps grants
 * U-mode RWX over all of internal SRAM.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

#define BIT_GO		(1u << 0)
#define BIT_ACK		(1u << 1)
#define STACK_SZ	1536u

/* Just above HP peri NAPOT (0x50000000 / 2MiB → ends 0x50200000). */
#define FORBIDDEN_MMIO	((volatile uint32_t *)(uintptr_t)0x50200010u)

static ulmk_notif_t g_sync;
static volatile int g_armed;
static volatile int g_survived;

static void user_faulter(void *arg)
{
	uint32_t bits = 0u;
	volatile uint32_t sink;

	(void)arg;
	(void)ulmk_notif_wait(g_sync, BIT_GO, &bits);
	g_armed = 1;
	(void)ulmk_notif_signal(g_sync, BIT_ACK);
	sink = *FORBIDDEN_MMIO;
	(void)sink;
	g_survived = 1;
	ulmk_thread_exit();
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	uint32_t bits = 0u;
	int i;
	int dead;
	int ok;

	board_services_init(info);
	board_console_puts("PMP_NEG begin\r\n");

	g_sync = ulmk_notif_create();
	if (g_sync == ULMK_NOTIF_INVALID) {
		board_console_puts("PMP_NEG: FAIL notif\r\n");
		ulmk_thread_exit();
	}

	g_armed = 0;
	g_survived = 0;
	attr.name       = "pmp_bad";
	attr.entry      = user_faulter;
	attr.priority   = 20u;
	attr.stack_size = STACK_SZ;
	attr.privilege  = ULMK_PRIV_USER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("PMP_NEG: FAIL spawn\r\n");
		ulmk_thread_exit();
	}

	(void)ulmk_notif_signal(g_sync, BIT_GO);
	(void)ulmk_notif_wait(g_sync, BIT_ACK, &bits);
	for (i = 0; i < 400; i++)
		(void)ulmk_thread_yield();

	dead = (ulmk_thread_priority_get(tid) < 0) ||
	       (g_armed && !g_survived);
	ok = dead && g_armed && !g_survived;
	if (ok)
		board_console_puts("PMP_NEG: PASS\r\n");
	else
		board_console_printf(
			"PMP_NEG: FAIL armed=%d survived=%d prio=%d\r\n",
			g_armed, g_survived,
			ulmk_thread_priority_get(tid));

	for (;;)
		board_timer_sleep_us(1000000u);
}
