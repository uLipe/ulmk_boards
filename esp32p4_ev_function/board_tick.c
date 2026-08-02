/* SPDX-License-Identifier: MIT */
/*
 * SYSTIMER TARGET0 periodic tick → INTMTX → CLIC IRQ 16.
 *
 * MMIO only (no IDF HAL link).  Period mode so ack only clears INT.
 */
#include <stdint.h>
#include <ulmk_arch.h>
#include "board_config.h"

#include <ulmk/board.h>

#define ST_BASE			ULMK_BOARD_SYSTIMER_BASE
#define ST_CONF			(ST_BASE + 0x00u)
#define ST_TARGET0_CONF		(ST_BASE + 0x34u)
#define ST_COMP0_LOAD		(ST_BASE + 0x50u)
#define ST_INT_ENA		(ST_BASE + 0x64u)
#define ST_INT_CLR		(ST_BASE + 0x6cu)

/* conf: clk_en=bit31, unit0_work=bit30, target0_work=bit24 (soc/systimer_struct.h) */
#define ST_CONF_CLK_EN		(1u << 31)
#define ST_CONF_UNIT0_WORK	(1u << 30)
#define ST_CONF_TARGET0_WORK	(1u << 24)

#define ST_T0_PERIOD_MODE	(1u << 30)
#define ST_T0_PERIOD_MASK	0x03FFFFFFu

#define ST_INT0			(1u << 0)

#define CLIC_INT_THRESH_REG	(ULMK_BOARD_CLIC_BASE + 0x08u)

#define HP_CLKRST_BASE		0x500E6000u
#define HP_SOC_CLK_CTRL2	(HP_CLKRST_BASE + 0x1cu)
#define HP_SYSTIMER_APB_EN	(1u << 23)

static inline void st_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)off = v;
}

static inline uint32_t st_rd(uint32_t off)
{
	return *(volatile uint32_t *)(uintptr_t)off;
}

static inline volatile uint8_t *clic_ie(uint32_t irq)
{
	return (volatile uint8_t *)(uintptr_t)
		(ULMK_BOARD_CLIC_BASE + 0x1000u + irq * 4u + 1u);
}

static inline volatile uint8_t *clic_attr(uint32_t irq)
{
	return (volatile uint8_t *)(uintptr_t)
		(ULMK_BOARD_CLIC_BASE + 0x1000u + irq * 4u + 2u);
}

static inline volatile uint8_t *clic_ctl(uint32_t irq)
{
	return (volatile uint8_t *)(uintptr_t)
		(ULMK_BOARD_CLIC_BASE + 0x1000u + irq * 4u + 3u);
}

void ulmk_board_tick_init(uint32_t tick_hz)
{
	uint32_t period;
	uint32_t conf;
	volatile uint32_t *clk2;

	/*
	 * SYSTIMER is shared.  Only CPU0 programs TARGET0; secondaries skip
	 * the hardware tick.  Arch returns timer_wheel_cpu=0 so sleeps on
	 * CPU1 still land on CPU0's wheel; expire + IPI wakes the remote.
	 */
	if (ulmk_arch_cpu_id() != 0u) {
		*(volatile uint32_t *)(uintptr_t)CLIC_INT_THRESH_REG = 0u;
		__asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));
		return;
	}

	if (tick_hz == 0u)
		tick_hz = 1000u;

	period = ULMK_BOARD_TICK_CLOCK_HZ / tick_hz;
	if (period == 0u)
		period = 1u;
	if (period > ST_T0_PERIOD_MASK)
		period = ST_T0_PERIOD_MASK;

	/* Ensure SYSTIMER APB clock (default-on; force in case BL gated it). */
	clk2 = (volatile uint32_t *)(uintptr_t)HP_SOC_CLK_CTRL2;
	*clk2 |= HP_SYSTIMER_APB_EN;

	/* Unmask all CLIC priority levels. */
	*(volatile uint32_t *)(uintptr_t)CLIC_INT_THRESH_REG = 0u;

	/*
	 * Driver lines are routed by the kernel when they are bound.  The tick
	 * never goes through that path, so it routes itself here.
	 */
	ulmk_board_irq_connect(ULMK_BOARD_IRQ_TICK);

	/* Tick is board glue, not a driver: it owns its CLIC slot directly.
	 * Same ctl level as every other line — see trap-irq-flat-priority. */
	*clic_attr(ULMK_BOARD_CLIC_IRQ_TICK) = 0u; /* non-vectored */
	*clic_ctl(ULMK_BOARD_CLIC_IRQ_TICK)  = (1u << 5);
	*clic_ie(ULMK_BOARD_CLIC_IRQ_TICK)   = 1u;

	conf = st_rd(ST_CONF);
	conf |= ST_CONF_CLK_EN | ST_CONF_UNIT0_WORK;
	st_wr(ST_CONF, conf);

	st_wr(ST_TARGET0_CONF, ST_T0_PERIOD_MODE | (period & ST_T0_PERIOD_MASK));
	st_wr(ST_COMP0_LOAD, 1u);

	conf = st_rd(ST_CONF);
	conf |= ST_CONF_TARGET0_WORK;
	st_wr(ST_CONF, conf);

	st_wr(ST_INT_ENA, ST_INT0);
	st_wr(ST_INT_CLR, ST_INT0);

	__asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));
}

void ulmk_board_tick_ack(void)
{
	st_wr(ST_INT_CLR, ST_INT0);
}
