/* SPDX-License-Identifier: MIT */
/*
 * Board timer — SysTick wheel via ulmk_sleep_ms; free-running TIM2 for
 * board_timer_now_ticks() (userspace-mapped; DWT is privileged-only).
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_timer.h"
#include "board_config.h"

#define TIM_CR1		0x00u
#define TIM_PSC		0x28u
#define TIM_ARR		0x2Cu
#define TIM_CNT		0x24u
#define TIM_EGR		0x14u
#define TIM_CR1_CEN	(1u << 0)
#define TIM_EGR_UG	(1u << 0)

static volatile uint32_t *g_tim
	__attribute__((section(".user_bss")));

static inline uint32_t tim_w(uint32_t off)
{
	return off / sizeof(uint32_t);
}

uint32_t board_timer_now_ticks(void)
{
	if (!g_tim)
		return 0u;
	return g_tim[tim_w(TIM_CNT)];
}

uint32_t board_timer_ticks_to_ns(uint32_t dt)
{
	uint64_t ns;

	ns = ((uint64_t)dt * 1000000000ull) / (uint64_t)ULMK_BOARD_FSTM_HZ;
	if (ns > 0xFFFFFFFFu)
		return 0xFFFFFFFFu;
	return (uint32_t)ns;
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

	g_tim = (volatile uint32_t *)ulmk_mem_map(
		(void *)(uintptr_t)ULMK_BOARD_HIL_TIMER_BASE,
		ULMK_BOARD_HIL_TIMER_SIZE,
		ULMK_PERM_READ | ULMK_PERM_WRITE,
		ULMK_MMAP_PERIPH);
	if (!g_tim)
		return ULMK_TID_INVALID;

	/* Free-running 32-bit @ timer kernel clock (2×PCLK1 when APB1≠1). */
	g_tim[tim_w(TIM_CR1)] = 0u;
	g_tim[tim_w(TIM_PSC)] = 0u;
	g_tim[tim_w(TIM_ARR)] = 0xFFFFFFFFu;
	g_tim[tim_w(TIM_EGR)] = TIM_EGR_UG;
	g_tim[tim_w(TIM_CR1)] = TIM_CR1_CEN;

	ulmk_tick_start();
	return (ulmk_tid_t)1u;
}
