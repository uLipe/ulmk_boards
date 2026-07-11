/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_timer.c — STM0 timer service (userspace).
 *
 * sleep_us polls STM0.TIM0 inside the timer server.  Compare-match IRQ is
 * left disabled: enabling CMP0EN without re-arming CMP0 asserts SR0
 * continuously, and Class-1 faults on the ISP are fatal.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_timer.h"

#define BOARD_TIMER_STM0_MAP_SIZE	0x100u
#define BOARD_TIMER_MSG_SLEEP		1u

#define STM0_TIM0	(ULMK_BOARD_STM0_BASE + 0x010u)
#define STM0_CMCON	(ULMK_BOARD_STM0_BASE + 0x038u)
#define STM0_ICR	(ULMK_BOARD_STM0_BASE + 0x03Cu)
#define STM0_ISCR	(ULMK_BOARD_STM0_BASE + 0x040u)

static ulmk_ep_t          g_ep __attribute__((section(".user_bss")));
static volatile uint32_t *g_stm0 __attribute__((section(".user_bss")));

static inline uint32_t stm0_off(uint32_t reg)
{
	return (reg - ULMK_BOARD_STM0_BASE) / sizeof(uint32_t);
}

static uint32_t us_to_ticks(uint32_t us)
{
	uint64_t ticks;

	ticks = ((uint64_t)us * (uint64_t)ULMK_BOARD_FSTM_HZ) / 1000000u;
	if (ticks == 0u)
		ticks = 1u;
	if (ticks > 0xFFFFFFFFu)
		ticks = 0xFFFFFFFFu;
	return (uint32_t)ticks;
}

uint32_t board_timer_now_ticks(void)
{
	if (!g_stm0)
		return 0u;
	return g_stm0[stm0_off(STM0_TIM0)];
}

uint32_t board_timer_ticks_to_ns(uint32_t dt)
{
	uint64_t ns;

	ns = ((uint64_t)dt * 1000000000ull) / (uint64_t)ULMK_BOARD_FSTM_HZ;
	if (ns > 0xFFFFFFFFu)
		return 0xFFFFFFFFu;
	return (uint32_t)ns;
}

static void stm0_busy_wait(uint32_t delta_ticks)
{
	uint32_t start;

	if (delta_ticks == 0u)
		delta_ticks = 1u;

	start = g_stm0[stm0_off(STM0_TIM0)];
	while ((g_stm0[stm0_off(STM0_TIM0)] - start) < delta_ticks) {
		/* TIM0 free-running poll — wrap-safe via unsigned subtract. */
	}
}

static void timer_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	(void)arg;

	reply.label    = 0u;
	reply.words[0] = 0u;

	for (;;) {
		ulmk_ep_recv(g_ep, &msg, &sender);
		if (msg.label != BOARD_TIMER_MSG_SLEEP) {
			ulmk_ep_reply(sender, &reply);
			continue;
		}

		stm0_busy_wait(us_to_ticks(msg.words[0]));
		ulmk_ep_reply(sender, &reply);
	}
}

void board_timer_sleep_us(uint32_t us)
{
	ulmk_msg_t msg;

	msg.label    = BOARD_TIMER_MSG_SLEEP;
	msg.words[0] = us;
	ulmk_ep_call(g_ep, &msg);
}

ulmk_tid_t board_timer_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t         tid;
	uint32_t           t0;
	uint32_t           t1;
	uint32_t           i;

	(void)info;

	g_ep = ulmk_ep_create();
	if (g_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_stm0 = (volatile uint32_t *)ulmk_mem_map(
		(void *)(uintptr_t)ULMK_BOARD_STM0_BASE,
		BOARD_TIMER_STM0_MAP_SIZE,
		ULMK_PERM_READ | ULMK_PERM_WRITE,
		ULMK_MMAP_PERIPH);
	if (!g_stm0)
		return ULMK_TID_INVALID;

	g_stm0[stm0_off(STM0_ISCR)] = 0x1u;
	g_stm0[stm0_off(STM0_CMCON)] = 0x0000001Fu;
	g_stm0[stm0_off(STM0_ICR)]   = 0u;

	t0 = g_stm0[stm0_off(STM0_TIM0)];
	for (i = 0u; i < 1000000u; i++) {
		t1 = g_stm0[stm0_off(STM0_TIM0)];
		if (t1 != t0)
			break;
	}
	if (t1 == t0)
		return ULMK_TID_INVALID;

	attr.name       = "btimer";
	attr.entry      = timer_server;
	attr.arg        = NULL;
	attr.priority   = 2u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;

	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
