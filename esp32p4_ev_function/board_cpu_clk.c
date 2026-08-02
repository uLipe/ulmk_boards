/* SPDX-License-Identifier: MIT */
/*
 * CPU clock: CPLL @ 400 MHz → CPU/MEM/SYS/APB = 400/200/200/100.
 *
 * The 2nd-stage IDF bootloader leaves this board at ~90 MHz (CPLL 360 / 4
 * with SELECTS_REV_LESS_V3).  SW LVGL render stays CPU-bound until we raise
 * the root clock.  Sequence mirrors esp-idf rtc_clk_cpu_freq_set_config()
 * for the 400 MHz path (hw_ver3 / ECO1+ CPLL analog coeffs).
 */
#include <stdint.h>
#include "board_cpu_clk.h"
#include "board_console.h"

#define PMU_BASE		0x50115000u
#define PMU_IMM_HP_CK_POWER	(PMU_BASE + 0xccu)
#define PMU_TIE_HIGH_XPD_CPLL		(1u << 27)
#define PMU_TIE_HIGH_XPD_CPLL_I2C	(1u << 23)
#define PMU_TIE_HIGH_GLOBAL_CPLL_ICG	(1u << 17)

#define HP_CLKRST		0x500E6000u
#define ROOT_CLK_CTRL0		(HP_CLKRST + 0x04u)
#define ROOT_CLK_CTRL1		(HP_CLKRST + 0x08u)
#define ROOT_CLK_CTRL2		(HP_CLKRST + 0x0cu)
#define ANA_PLL_CTRL0		(HP_CLKRST + 0xbcu)
#define CPLL_CAL_END		(1u << 2)
#define CPLL_CAL_STOP		(1u << 3)
#define SOC_CLK_DIV_UPDATE	(1u << 4)

#define LP_CLKRST_BASE		0x50111000u
#define LP_HP_CLK_CTRL		(LP_CLKRST_BASE + 0x40u)

#define LPPERI_CLK_EN		0x50120000u
#define LPPERI_I2CMST_EN	(1u << 27)

#define I2C_ANA_BASE		0x50124000u
#define I2C_ANA_CTRL0		(I2C_ANA_BASE + 0x00u)
#define I2C_ANA_CONF1		(I2C_ANA_BASE + 0x1cu)
#define I2C_ANA_CONF2		(I2C_ANA_BASE + 0x20u)
#define I2C_ANA_CLK160M		(I2C_ANA_BASE + 0x34u)

#define I2C_CPLL		0x67u
#define REGI2C_BUSY		(1u << 25)
#define REGI2C_WR		(1u << 24)

/* CPLL analog regs (soc/regi2c_cpll.h) — ECO1+ 400 MHz coeffs. */
#define CPLL_OC_REF_DIV		2u
#define CPLL_OC_DIV_7_0		3u
#define CPLL_OC_DCUR		6u
#define CPLL_OC_DCHGP_LSB	4u
#define CPLL_OC_ENB_FCAL_LSB	7u
#define CPLL_OC_DHREF_SEL_LSB	4u
#define CPLL_OC_DLREF_SEL_LSB	6u

#define CPU_SRC_XTAL		0u
#define CPU_SRC_CPLL		1u

extern void esp_rom_set_cpu_ticks_per_us(int ticks);

static int g_cpu_400m;

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void settle(uint32_t n)
{
	volatile uint32_t i;

	for (i = 0u; i < n; i++)
		;
}

static void bus_update(void)
{
	uint32_t v = rd(ROOT_CLK_CTRL0);

	wr(ROOT_CLK_CTRL0, v | SOC_CLK_DIV_UPDATE);
	while (rd(ROOT_CLK_CTRL0) & SOC_CLK_DIV_UPDATE)
		;
}

static void cpu_set_src(uint32_t src)
{
	uint32_t v = rd(LP_HP_CLK_CTRL);

	v = (v & ~0x3u) | (src & 0x3u);
	wr(LP_HP_CLK_CTRL, v);
}

static void set_cpu_div(uint32_t integer)
{
	uint32_t v = rd(ROOT_CLK_CTRL0);

	/* cpu_div_num [12:5] = integer - 1; clear num/den. */
	v &= ~(((0xFFu << 5) | (0xFFu << 13) | (0xFFu << 21)));
	v |= ((integer - 1u) & 0xFFu) << 5;
	wr(ROOT_CLK_CTRL0, v);
}

static void set_mem_div(uint32_t divider)
{
	uint32_t v = rd(ROOT_CLK_CTRL1);

	v = (v & ~0xFFu) | ((divider - 1u) & 0xFFu);
	wr(ROOT_CLK_CTRL1, v);
}

static void set_sys_div(uint32_t divider)
{
	uint32_t v = rd(ROOT_CLK_CTRL1);

	v &= ~(0xFFu << 24);
	v |= ((divider - 1u) & 0xFFu) << 24;
	wr(ROOT_CLK_CTRL1, v);
}

static void set_apb_div(uint32_t divider)
{
	uint32_t v = rd(ROOT_CLK_CTRL2);

	v &= ~(0xFFu << 16);
	v |= ((divider - 1u) & 0xFFu) << 16;
	wr(ROOT_CLK_CTRL2, v);
}

static void regi2c_wait(void)
{
	volatile uint32_t guard = 0u;

	while ((rd(I2C_ANA_CTRL0) & REGI2C_BUSY) && guard < 200000u)
		guard++;
}

static void regi2c_write(uint8_t block, uint8_t reg, uint8_t data)
{
	uint32_t temp;

	wr(I2C_ANA_CONF2, 0u);
	wr(I2C_ANA_CONF1, 0u);
	regi2c_wait();
	temp = ((uint32_t)block) | ((uint32_t)reg << 8) | REGI2C_WR |
	       ((uint32_t)data << 16);
	wr(I2C_ANA_CTRL0, temp);
	regi2c_wait();
}

static void cpll_enable(void)
{
	wr(PMU_IMM_HP_CK_POWER,
	   PMU_TIE_HIGH_XPD_CPLL | PMU_TIE_HIGH_XPD_CPLL_I2C);
	wr(PMU_IMM_HP_CK_POWER, PMU_TIE_HIGH_GLOBAL_CPLL_ICG);
}

static void cpu_to_xtal(void)
{
	cpu_set_src(CPU_SRC_XTAL);
	set_cpu_div(1u);
	set_mem_div(1u);
	set_sys_div(1u);
	set_apb_div(1u);
	bus_update();
	esp_rom_set_cpu_ticks_per_us(40);
}

static int cpll_configure_400m(void)
{
	uint8_t lref;
	uint8_t div7_0;
	uint8_t dcur;
	uint32_t guard;

	/* Enable analog I2C master (same path as board_mpll). */
	wr(LPPERI_CLK_EN, rd(LPPERI_CLK_EN) | LPPERI_I2CMST_EN);
	wr(I2C_ANA_CLK160M, rd(I2C_ANA_CLK160M) | (1u << 0) | (1u << 28));
	settle(8000u);

	/* CPLL CALIBRATION START */
	wr(ANA_PLL_CTRL0, rd(ANA_PLL_CTRL0) & ~CPLL_CAL_STOP);

	/*
	 * ECO1+ 400 MHz: div7_0=10, div_ref=0, dchgp=5, dcur=3.
	 * (clk_ll_cpll_set_config, chip_revision >= 1)
	 */
	lref = (0u << CPLL_OC_ENB_FCAL_LSB) | (5u << CPLL_OC_DCHGP_LSB) | 0u;
	div7_0 = 10u;
	dcur = (1u << CPLL_OC_DLREF_SEL_LSB) | (3u << CPLL_OC_DHREF_SEL_LSB) |
	       3u;
	regi2c_write(I2C_CPLL, CPLL_OC_REF_DIV, lref);
	regi2c_write(I2C_CPLL, CPLL_OC_DIV_7_0, div7_0);
	regi2c_write(I2C_CPLL, CPLL_OC_DCUR, dcur);

	guard = 0u;
	while (!(rd(ANA_PLL_CTRL0) & CPLL_CAL_END) && guard < 2000000u)
		guard++;
	settle(400u); /* ~10 us at XTAL */
	wr(ANA_PLL_CTRL0, rd(ANA_PLL_CTRL0) | CPLL_CAL_STOP);

	return (rd(ANA_PLL_CTRL0) & CPLL_CAL_END) ? 0 : -1;
}

/*
 * A core that has a memory access in flight while CPU/MEM/APB rates move can
 * latch a corrupt cache line and later stall on it.  Park the peer core in the
 * PMU stall state across every switch, as ESP-IDF does.
 */
#define PMU_CPU_SW_STALL	0x50115200u
#define HP_SYS_CORESTALLED_ST	0x500E5064u
#define PMU_STALL_CODE		0x86u
#define PMU_RUN_CODE		0xFFu

void board_cpu_peer_stall(uint32_t peer)
{
	uint32_t shift = (peer == 0u) ? 24u : 16u;
	uint32_t bit = 1u << peer;
	uint32_t guard = 0u;

	wr(PMU_CPU_SW_STALL, (rd(PMU_CPU_SW_STALL) & ~(0xFFu << shift)) |
			     (PMU_STALL_CODE << shift));
	while ((rd(HP_SYS_CORESTALLED_ST) & bit) == 0u && guard < 100000u)
		guard++;
}

void board_cpu_peer_unstall(uint32_t peer)
{
	uint32_t shift = (peer == 0u) ? 24u : 16u;
	uint32_t bit = 1u << peer;
	uint32_t guard = 0u;

	wr(PMU_CPU_SW_STALL, (rd(PMU_CPU_SW_STALL) & ~(0xFFu << shift)) |
			     (PMU_RUN_CODE << shift));
	while ((rd(HP_SYS_CORESTALLED_ST) & bit) != 0u && guard < 100000u)
		guard++;
}

static void cpu_to_cpll_400m(void)
{
	/*
	 * Upscale order: APB → SYS → MEM → CPU, then switch mux last
	 * (avoids illegal intermediate MEM/APB rates).
	 * CPLL 400 → CPU 400 / MEM 200 / SYS 200 / APB 100.
	 */
	set_apb_div(2u);
	bus_update();
	set_sys_div(1u);
	bus_update();
	set_mem_div(2u);
	bus_update();
	set_cpu_div(1u);
	bus_update();

	cpu_set_src(CPU_SRC_CPLL);
	esp_rom_set_cpu_ticks_per_us(400);
}

int board_cpu_clk_set_400m(void)
{
	if (g_cpu_400m)
		return 0;

	cpll_enable();
	cpu_to_xtal();
	if (cpll_configure_400m() != 0) {
		board_console_printf("ulmk: cpll 400m cal timeout\n");
		return -1;
	}
	cpu_to_cpll_400m();
	g_cpu_400m = 1;
	board_console_printf("ulmk: cpu 400m ok\n");
	return 0;
}
