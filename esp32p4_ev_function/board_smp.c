/* SPDX-License-Identifier: MIT */
/*
 * ESP32-P4 SMP: APP CPU bring-up + HP_SYSTEM soft IPI.
 *
 * Bring-up mirrors IDF start_other_core(): unstall, enable core1 clock /
 * clear global reset, then ets_set_appcpu_boot_addr(_start).
 * IPI: CPU_INT_FROM_CPU_n → INTMTX (this core) → CLIC IRQ_IPI.
 */
#include <stdint.h>
#include <ulmk/board.h>
#include <ulmk_arch.h>
#include "board_config.h"

#if ULMK_ARCH_HAVE_BOARD_CPU_START

/* ROM — provided by esp32p4.rom.ld */
void ets_set_appcpu_boot_addr(uint32_t addr);

#define PMU_CPU_SW_STALL_REG	0x50115200u /* DR_REG_PMU_BASE + 0x200 */
#define PMU_STALL_CODE_RUN	0xFFu
#define PMU_CORE1_STALL_SHIFT	16u

#define HP_CLKRST_BASE		0x500E6000u
#define HP_SOC_CLK_CTRL0	(HP_CLKRST_BASE + 0x14u)
#define HP_RST_EN0		(HP_CLKRST_BASE + 0xc0u)
#define HP_CORE1_CPU_CLK_EN	(1u << 4)
#define HP_RST_EN_CORE1_GLOBAL	(1u << 8)

#define HP_CORESTALLED_ST	(ULMK_BOARD_HP_SYS_BASE + 0x64u)
#define HP_CORE1_STALLED	(1u << 1)

static inline void wr32(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd32(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void core1_unstall(void)
{
	uint32_t v;
	uint32_t spins;

	v = rd32(PMU_CPU_SW_STALL_REG);
	v &= ~(0xFFu << PMU_CORE1_STALL_SHIFT);
	v |= (PMU_STALL_CODE_RUN << PMU_CORE1_STALL_SHIFT);
	wr32(PMU_CPU_SW_STALL_REG, v);

	for (spins = 0u; spins < 100000u; spins++) {
		if ((rd32(HP_CORESTALLED_ST) & HP_CORE1_STALLED) == 0u)
			break;
	}
}

static void core1_clock_and_reset(void)
{
	uint32_t v;

	v = rd32(HP_SOC_CLK_CTRL0);
	if ((v & HP_CORE1_CPU_CLK_EN) == 0u)
		wr32(HP_SOC_CLK_CTRL0, v | HP_CORE1_CPU_CLK_EN);

	v = rd32(HP_RST_EN0);
	if ((v & HP_RST_EN_CORE1_GLOBAL) != 0u)
		wr32(HP_RST_EN0, v & ~HP_RST_EN_CORE1_GLOBAL);
}

void ulmk_board_cpu_start(uint32_t cpu_id, void (*entry)(void))
{
	if (cpu_id != 1u || !entry)
		return;

	/*
	 * IDF order: unstall → clock/reset → boot address.  Core1 sits in
	 * ROM until the boot address is non-zero, then jumps to @entry.
	 */
	core1_unstall();
	core1_clock_and_reset();
	__asm__ volatile("fence rw, rw" ::: "memory");
	ets_set_appcpu_boot_addr((uint32_t)(uintptr_t)entry);
}

#endif /* ULMK_ARCH_HAVE_BOARD_CPU_START */

#if ULMK_ARCH_HAVE_BOARD_IPI

static inline void ipi_wr32(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
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

static void intmtx_route_cpu(uint32_t cpu, uint8_t source, uint8_t slot)
{
	volatile uint32_t *map;
	uintptr_t base;

	base = (uintptr_t)ULMK_BOARD_INTMTX_BASE +
	       (uintptr_t)cpu * (uintptr_t)ULMK_BOARD_INTMTX_CORE_STRIDE;
	map = (volatile uint32_t *)(base + 4u * (uint32_t)source);
	*map = (*map & ~0x3Fu) | ((uint32_t)slot & 0x3Fu);
}

void ulmk_board_ipi_clear_self(void)
{
	uint32_t cpu = ulmk_arch_cpu_id();

	if (cpu == 0u)
		ipi_wr32(ULMK_BOARD_HP_FROM_CPU0_REG, 0u);
	else if (cpu == 1u)
		ipi_wr32(ULMK_BOARD_HP_FROM_CPU1_REG, 0u);
}

void ulmk_board_ipi_arm_self(void)
{
	uint32_t cpu = ulmk_arch_cpu_id();
	uint8_t source;

	if (cpu >= (uint32_t)ULMK_ARCH_NUM_CPU)
		return;

	source = (cpu == 0u) ? (uint8_t)ETS_FROM_CPU_INTR0_SOURCE
			     : (uint8_t)ETS_FROM_CPU_INTR1_SOURCE;

	ulmk_board_ipi_clear_self();

	intmtx_route_cpu(cpu, source, (uint8_t)ULMK_BOARD_CLIC_IRQ_IPI);

	*(volatile uint32_t *)(uintptr_t)(ULMK_BOARD_CLIC_BASE + 0x08u) = 0u;
	*clic_attr(ULMK_BOARD_CLIC_IRQ_IPI) = 0u; /* non-vectored */
	*clic_ctl(ULMK_BOARD_CLIC_IRQ_IPI)  = (1u << 5);
	*clic_ie(ULMK_BOARD_CLIC_IRQ_IPI)   = 1u;

	__asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));
}

void ulmk_board_ipi_send(uint32_t cpu_id)
{
	if (cpu_id == 0u)
		ipi_wr32(ULMK_BOARD_HP_FROM_CPU0_REG, 1u);
	else if (cpu_id == 1u)
		ipi_wr32(ULMK_BOARD_HP_FROM_CPU1_REG, 1u);
}

#endif /* ULMK_ARCH_HAVE_BOARD_IPI */
