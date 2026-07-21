/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_timer.c — thin wrapper over kernel sleep.
 *
 * STM0 CMP0/SR0 is owned by the arch tick.  TIM0 is mapped read-only for
 * board_timer_now_ticks() (WCET / silicon_baseline).
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_timer.h"

#define BOARD_TIMER_STM0_MAP_SIZE	0x100u
#define STM0_TIM0	(ULMK_BOARD_STM0_BASE + 0x010u)

static volatile uint32_t *g_stm0 __attribute__((section(".user_bss")));

static inline uint32_t stm0_off(uint32_t reg)
{
	return (reg - ULMK_BOARD_STM0_BASE) / sizeof(uint32_t);
}

void board_timer_sleep_us(uint32_t us)
{
	uint32_t ms = (us + 999u) / 1000u;

	if (ms == 0u)
		ms = 1u;
	(void)ulmk_sleep_ms(ms);
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

ulmk_tid_t board_timer_start(const ulmk_boot_info_t *info)
{
	(void)info;

	g_stm0 = (volatile uint32_t *)ulmk_mem_map(
		(void *)(uintptr_t)ULMK_BOARD_STM0_BASE,
		BOARD_TIMER_STM0_MAP_SIZE,
		ULMK_PERM_READ,
		ULMK_MMAP_PERIPH);
	if (!g_stm0)
		return ULMK_TID_INVALID;
	ulmk_tick_start();
	return (ulmk_tid_t)1u;
}
