/* SPDX-License-Identifier: MIT */
/*
 * MSPI MPLL bring-up — LDO VO2 (PSRAM 1.8 V) + ANA regi2c + cal.
 * Call only after .bss is cleared.
 */
#include <stdint.h>
#include "board_mpll.h"

#include "board_console.h"

#define PMU_BASE		0x50115000u
#define PMU_RF_PWC		(PMU_BASE + 0x15cu)
#define PMU_MSPI_PHY_XPD	(1u << 24)

/* LDO unit1 = VO2 (PSRAM) → ext_ldo index 3 → P1_0P1A */
#define LDO_VO2_CTRL		(PMU_BASE + 0x1d0u)
#define LDO_VO2_ANA		(PMU_BASE + 0x1d4u)
#define LDO_FORCE_TIEH_SEL	(1u << 7)
#define LDO_XPD			(1u << 8)
#define LDO_TIEH		(1u << 14)
#define LDO_ANA_MUL_S		23
#define LDO_ANA_EN_VDET		(1u << 26)
#define LDO_ANA_DREF_S		28

#define LP_CLKRST_HP_CLK	0x50111040u
#define LP_MPLL_500M_EN		(1u << 28)

#define LPPERI_CLK_EN		0x50120000u
#define LPPERI_I2CMST_EN	(1u << 27)

#define I2C_ANA_BASE		0x50124000u
#define I2C_ANA_CTRL0		(I2C_ANA_BASE + 0x00u)
#define I2C_ANA_CONF1		(I2C_ANA_BASE + 0x1cu)
#define I2C_ANA_CONF2		(I2C_ANA_BASE + 0x20u)
#define I2C_ANA_CLK160M		(I2C_ANA_BASE + 0x34u)

#define ANA_PLL_CTRL0		0x500E60BCu
#define MSPI_CAL_END		(1u << 8)
#define MSPI_CAL_STOP		(1u << 9)

#define I2C_MPLL		0x63u
#define MPLL_SEL		(1u << 9)

#define REGI2C_BUSY		(1u << 25)
#define REGI2C_WR		(1u << 24)

static int g_mpll_ok;

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

static void ldo_vo2_psram_1v8(void)
{
	uint32_t ctrl;
	uint32_t ana;

	/*
	 * Channel 2 / VO2 → ~1.8 V (Function EV PSRAM rail).
	 * dref=8, mul=4 ≈ 0.9 V * (1 + 0.25*4) = 1.8 V without efuse cal.
	 */
	ana = (4u << LDO_ANA_MUL_S) | LDO_ANA_EN_VDET | (8u << LDO_ANA_DREF_S);
	wr(LDO_VO2_ANA, ana);

	ctrl = rd(LDO_VO2_CTRL);
	ctrl |= LDO_FORCE_TIEH_SEL | LDO_XPD;
	ctrl &= ~LDO_TIEH;
	wr(LDO_VO2_CTRL, ctrl);
	settle(40000u); /* ~rail settle before MPLL */
}

static void regi2c_wait(void)
{
	volatile uint32_t guard = 0u;

	while ((rd(I2C_ANA_CTRL0) & REGI2C_BUSY) && guard < 200000u)
		guard++;
}

static uint8_t regi2c_read(uint8_t block, uint8_t reg)
{
	uint32_t temp;

	wr(I2C_ANA_CONF2, 0u);
	wr(I2C_ANA_CONF1, 0u);
	wr(I2C_ANA_CONF2, MPLL_SEL);
	regi2c_wait();
	temp = ((uint32_t)block) | ((uint32_t)reg << 8);
	wr(I2C_ANA_CTRL0, temp);
	regi2c_wait();
	return (uint8_t)((rd(I2C_ANA_CTRL0) >> 16) & 0xFFu);
}

static void regi2c_write(uint8_t block, uint8_t reg, uint8_t data)
{
	uint32_t temp;

	wr(I2C_ANA_CONF2, 0u);
	wr(I2C_ANA_CONF1, 0u);
	wr(I2C_ANA_CONF2, MPLL_SEL);
	regi2c_wait();
	temp = ((uint32_t)block) | ((uint32_t)reg << 8) | REGI2C_WR |
	       ((uint32_t)data << 16);
	wr(I2C_ANA_CTRL0, temp);
	regi2c_wait();
}

static int mpll_cal_once(void)
{
	uint8_t v;
	uint32_t guard;
	int ok;

	wr(ANA_PLL_CTRL0, rd(ANA_PLL_CTRL0) & ~MSPI_CAL_STOP);

	v = regi2c_read(I2C_MPLL, 3u);
	regi2c_write(I2C_MPLL, 3u, (uint8_t)(v | (3u << 4)));
	v = regi2c_read(I2C_MPLL, 1u);
	regi2c_write(I2C_MPLL, 1u, (uint8_t)(v & 0xDFu));
	regi2c_write(I2C_MPLL, 1u, (uint8_t)(v | (1u << 5)));
	/* 400 MHz: div=19, ref_div=1 → 40*(19+1)/(1+1) */
	regi2c_write(I2C_MPLL, 2u, (uint8_t)((19u << 3) | 1u));

	guard = 0u;
	while (!(rd(ANA_PLL_CTRL0) & MSPI_CAL_END) && guard < 2000000u)
		guard++;
	ok = (rd(ANA_PLL_CTRL0) & MSPI_CAL_END) ? 1 : 0;
	wr(ANA_PLL_CTRL0, rd(ANA_PLL_CTRL0) | MSPI_CAL_STOP);
	return ok ? 0 : -1;
}

int board_mpll_enable_400m(void)
{
	int attempt;

	if (g_mpll_ok)
		return 0;

	ldo_vo2_psram_1v8();

	wr(PMU_RF_PWC, rd(PMU_RF_PWC) | PMU_MSPI_PHY_XPD);
	wr(LP_CLKRST_HP_CLK, rd(LP_CLKRST_HP_CLK) | LP_MPLL_500M_EN);
	wr(LPPERI_CLK_EN, rd(LPPERI_CLK_EN) | LPPERI_I2CMST_EN);
	wr(I2C_ANA_CLK160M, rd(I2C_ANA_CLK160M) | (1u << 0) | (1u << 28));
	settle(8000u);

	for (attempt = 0; attempt < 3; attempt++) {
		if (mpll_cal_once() == 0) {
			g_mpll_ok = 1;
			board_console_printf("ulmk: mpll 400m ok\n");
			return 0;
		}
		settle(20000u);
	}

	board_console_printf("ulmk: mpll cal timeout\n");
	return -1;
}
