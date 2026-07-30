/* SPDX-License-Identifier: MIT */
/*
 * PMP negative smoke — two U-mode probes that must both trap and be killed:
 *
 *   1. a load outside the HP peripheral window   (no grant covers it)
 *   2. a load from kernel RAM                    (isolation from the kernel)
 *
 * The second one is the interesting one.  User RAM sits immediately above
 * kernel RAM, so a NAPOT window rounded out to the next power of two covers
 * both and quietly hands U-mode the kernel's data; only a TOR pair draws the
 * boundary where the linker put it.  A probe that survives here means the
 * grant is too wide, not that the fault handling is broken.
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

extern uint8_t _ulmk_kernel_data_start[];

static ulmk_notif_t g_sync;
static volatile int g_armed;
static volatile int g_survived;
static volatile uint32_t g_peeked;

static void probe_arm(void)
{
	uint32_t bits = 0u;

	(void)ulmk_notif_wait(g_sync, BIT_GO, &bits);
	g_armed = 1;
	(void)ulmk_notif_signal(g_sync, BIT_ACK);
}

static void user_faulter(void *arg)
{
	volatile uint32_t sink;

	(void)arg;
	probe_arm();
	sink = *FORBIDDEN_MMIO;
	(void)sink;
	g_survived = 1;
	ulmk_thread_exit();
}

static void user_peeker(void *arg)
{
	(void)arg;
	probe_arm();
	g_peeked = *(volatile uint32_t *)_ulmk_kernel_data_start;
	g_survived = 1;
	ulmk_thread_exit();
}

static int probe_died(const char *name, void (*entry)(void *))
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	uint32_t bits = 0u;
	int i;

	g_armed = 0;
	g_survived = 0;

	attr.name       = name;
	attr.entry      = entry;
	attr.priority   = 20u;
	attr.stack_size = STACK_SZ;
	attr.privilege  = ULMK_PRIV_USER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_printf("PMP_NEG: FAIL spawn %s\r\n", name);
		return 0;
	}

	(void)ulmk_notif_signal(g_sync, BIT_GO);
	(void)ulmk_notif_wait(g_sync, BIT_ACK, &bits);
	for (i = 0; i < 400; i++)
		(void)ulmk_thread_yield();

	/*
	 * Reaching the probe and never getting past it is the whole signal:
	 * priority_get still answers for a killed tid, so it cannot stand in
	 * for liveness here.
	 */
	if (!g_armed || g_survived) {
		board_console_printf(
			"PMP_NEG: FAIL %s armed=%d survived=%d\r\n",
			name, g_armed, g_survived);
		return 0;
	}
	return 1;
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	int ok;

	board_services_init(info);
	board_console_puts("PMP_NEG begin\r\n");

	g_sync = ulmk_notif_create();
	if (g_sync == ULMK_NOTIF_INVALID) {
		board_console_puts("PMP_NEG: FAIL notif\r\n");
		ulmk_thread_exit();
	}

	ok = probe_died("pmp_mmio", user_faulter);
	if (ok)
		board_console_puts("PMP_NEG: mmio ok\r\n");

	ok = probe_died("pmp_kram", user_peeker) && ok;
	if (ok)
		board_console_printf("PMP_NEG: kram ok (read %u)\r\n",
				     (uint32_t)g_peeked);

	if (ok)
		board_console_puts("PMP_NEG: PASS\r\n");

	for (;;)
		board_timer_sleep_us(1000000u);
}
