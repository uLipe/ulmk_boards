/* SPDX-License-Identifier: MIT */
/*
 * Board timer — kernel timing wheel wrapper (no busy-wait) plus a free-
 * running SYSTIMER snapshot for LVGL / FPS accounting.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_timer.h"
#include "board_config.h"

#define ST_MAP_SIZE		0x1000u
#define ST_UNIT0_OP_OFF		0x04u
#define ST_UNIT0_VALUE_HI_OFF	0x40u
#define ST_UNIT0_VALUE_LO_OFF	0x44u
#define ST_UNIT0_UPDATE		(1u << 30)
#define ST_UNIT0_VALID		(1u << 29)

static volatile uint32_t *g_st __attribute__((section(".user_bss")));

static int st_ensure_mapped(void)
{
	if (g_st)
		return 0;
	g_st = (volatile uint32_t *)ulmk_mem_map(
		(void *)(uintptr_t)ULMK_BOARD_SYSTIMER_BASE, ST_MAP_SIZE,
		ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	return g_st ? 0 : -1;
}

static inline uint32_t st_rd(uint32_t off)
{
	return g_st[off / sizeof(uint32_t)];
}

static inline void st_wr(uint32_t off, uint32_t v)
{
	g_st[off / sizeof(uint32_t)] = v;
}

uint32_t board_timer_now_ms(void)
{
	uint32_t lo;
	uint32_t hi;
	uint64_t ticks;
	uint32_t spins;

	if (st_ensure_mapped() != 0)
		return 0u;

	/*
	 * Snapshot: write UPDATE, wait VALID, then read hi/lo.  Using the
	 * 52-bit latch avoids the ~268 s wrap of the low word alone.
	 */
	st_wr(ST_UNIT0_OP_OFF, ST_UNIT0_UPDATE);
	for (spins = 0u; spins < 1000u; spins++) {
		if (st_rd(ST_UNIT0_OP_OFF) & ST_UNIT0_VALID)
			break;
	}
	hi = st_rd(ST_UNIT0_VALUE_HI_OFF) & 0xFFFFFu;
	lo = st_rd(ST_UNIT0_VALUE_LO_OFF);
	ticks = ((uint64_t)hi << 32) | (uint64_t)lo;
	return (uint32_t)(ticks / (uint64_t)(ULMK_BOARD_TICK_CLOCK_HZ / 1000u));
}

void board_timer_sleep_us(uint32_t us)
{
	uint32_t ms = (us + 999u) / 1000u;

	if (ms == 0u)
		ms = 1u;
	(void)ulmk_sleep_ms(ms);
}

ulmk_tid_t board_timer_start(const ulmk_boot_info_t *info)
{
	(void)info;
	(void)st_ensure_mapped();
	ulmk_tick_start();
	return (ulmk_tid_t)1u;
}
