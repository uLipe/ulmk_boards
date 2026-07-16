/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_init.c — early hardware bring-up (CPU0 only).
 *
 * Runs from ulmk_kern_start() before .data copy: no globals, no kernel API.
 *
 * Watchdog EndInit uses Infineon iLLD inlines (IfxScuWdt_*Inline) from
 * deps/illd_tc2x (TC27D, V1.22.0).  PLL / flash follow the same register
 * sequence as IfxScuCcu_init() for 20 MHz → 200 MHz, but without linking
 * IfxScuCcu.c (that file has .data and cannot run pre-relocation).
 */

#include <stdint.h>

#include "Ifx_Cfg.h"
#include "Scu/Std/IfxScuWdt.h"
#include "IfxAsclin_reg.h"
#include "IfxStm_reg.h"
#include "IfxI2c_reg.h"
#include "IfxVadc_reg.h"
#include "IfxCan_reg.h"
#include "IfxGtm_reg.h"
#include "IfxFlash_reg.h"
#include "_Impl/IfxScu_cfg.h"
#include "board_config.h"

#ifndef IFXSCUCCU_OSC_STABLECHK_TIME
#define IFXSCUCCU_OSC_STABLECHK_TIME	640
#endif

/* Busy-wait calibrated for ~100 MHz backup / early PLL (EVR / fback). */
static void busy_wait_us(uint32_t us)
{
	volatile uint32_t n = us * 25u;

	while (n-- != 0u) {
	}
}

static void wdt_disable_cpu0(void)
{
	uint16_t pw;

	pw = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[0]);
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw);
	MODULE_SCU.WDTCPU[0].CON1.B.DR = 1u;
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw);
}

static void wdt_disable_safety(void)
{
	uint16_t pw;

	pw = IfxScuWdt_getSafetyWatchdogPasswordInline();
	IfxScuWdt_clearSafetyEndinitInline(pw);
	MODULE_SCU.WDTS.CON1.B.DR = 1u;
	/* Clear double-SMU-reset latch before first service window (UM 7.2.1.10). */
	MODULE_SCU.WDTS.CON1.B.CLRIRF = 1u;
	IfxScuWdt_setSafetyEndinitInline(pw);
}

static int osc_wait_stable(void)
{
	int32_t timeout = IFXSCUCCU_OSC_STABLECHK_TIME;

	SCU_OSCCON.B.MODE = 0u;
	SCU_OSCCON.B.OSCVAL = (uint32_t)(IFX_CFG_SCU_XTAL_FREQUENCY / 2500000u) - 1u;
	SCU_OSCCON.B.OSCRES = 1u;

	while ((SCU_OSCCON.B.PLLLV == 0u) || (SCU_OSCCON.B.PLLHV == 0u)) {
		timeout--;
		if (timeout <= 0) {
			return 1;
		}
	}
	return 0;
}

/*
 * 20 MHz XTAL → 200 MHz fPLL (iLLD IFXSCU_CFG_*_20MHZ_200MHZ):
 *   initial: P=1, N=59, K2=5  → ~66.7 MHz then ramp K2: 4 → 3 → 2
 */
static int pll_init_20mhz_200mhz(void)
{
	uint16_t pw_cpu;
	uint16_t pw_sfty;
	uint8_t smu_trap;
	Ifx_SCU_CCUCON0 ccucon0;
	Ifx_SCU_CCUCON1 ccucon1;
	Ifx_FLASH_FCON fcon;

	pw_cpu = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[0]);
	pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();

	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	smu_trap = SCU_TRAPDIS.B.SMUT;
	SCU_TRAPDIS.B.SMUT = 1u;
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);

	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);

	while (SCU_CCUCON0.B.LCK != 0u) {
	}
	SCU_CCUCON0.B.CLKSEL = 0u;
	SCU_CCUCON0.B.UP = 1u;

	SCU_PLLCON0.B.SETFINDIS = 1u;

	while (SCU_CCUCON1.B.LCK != 0u) {
	}
	SCU_CCUCON1.B.INSEL = 1u;
	SCU_CCUCON1.B.UP = 1u;

	if (osc_wait_stable() != 0) {
		IfxScuWdt_setSafetyEndinitInline(pw_sfty);
		return -1;
	}

	IfxScuWdt_setSafetyEndinitInline(pw_sfty);

	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);

	while (SCU_PLLSTAT.B.K2RDY == 0u) {
	}
	SCU_PLLCON1.B.K2DIV = 5u; /* (6 - 1) initial */

	SCU_PLLCON0.B.PDIV = 1u;  /* (2 - 1) */
	SCU_PLLCON0.B.NDIV = 59u; /* (60 - 1) */
	SCU_PLLCON0.B.OSCDISCDIS = 1u;
	SCU_PLLCON0.B.PLLPWD = 0u;
	SCU_PLLCON0.B.CLRFINDIS = 1u;
	SCU_PLLCON0.B.PLLPWD = 1u;
	SCU_PLLCON0.B.RESLD = 1u;

	busy_wait_us(50u);

	while (SCU_PLLSTAT.B.VCOLOCK == 0u) {
	}

	SCU_PLLCON0.B.VCOBYP = 0u;

	while (SCU_CCUCON0.B.LCK != 0u) {
	}
	SCU_CCUCON0.B.CLKSEL = 1u;

	busy_wait_us(200u);

	ccucon0.U = SCU_CCUCON0.U & ~IFXSCU_CFG_CCUCON0_MASK;
	ccucon0.U |= (IFXSCU_CFG_CCUCON0_MASK & IFXSCU_CFG_CCUCON0);
	ccucon0.B.CLKSEL = 1u;
	ccucon0.B.UP = 1u;
	SCU_CCUCON0 = ccucon0;

	while (SCU_CCUCON1.B.LCK != 0u) {
	}
	ccucon1.U = SCU_CCUCON1.U & ~IFXSCU_CFG_CCUCON1_MASK;
	ccucon1.U |= (IFXSCU_CFG_CCUCON1_MASK & IFXSCU_CFG_CCUCON1);
	ccucon1.B.INSEL = 1u;
	ccucon1.B.UP = 1u;
	SCU_CCUCON1 = ccucon1;

	IfxScuWdt_setSafetyEndinitInline(pw_sfty);

	/* Flash wait states for 200 MHz (CPU EndInit). */
	fcon.U = FLASH0_FCON.U & ~IFXSCU_CFG_FLASH_WAITSTATE_MSK;
	fcon.U |= (IFXSCU_CFG_FLASH_WAITSTATE_MSK & IFXSCU_CFG_FLASH_WAITSTATE_VAL);
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	FLASH0_FCON = fcon;
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);

	/* K2 ramp: 120 → 150 → 200 MHz (iLLD steps use k2 = N-1). */
	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	while (SCU_PLLSTAT.B.K2RDY == 0u) {
	}
	SCU_PLLCON1.B.K2DIV = 4u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	busy_wait_us(100u);

	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	while (SCU_PLLSTAT.B.K2RDY == 0u) {
	}
	SCU_PLLCON1.B.K2DIV = 3u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	busy_wait_us(100u);

	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	while (SCU_PLLSTAT.B.K2RDY == 0u) {
	}
	SCU_PLLCON1.B.K2DIV = 2u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	busy_wait_us(100u);

	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	SCU_PLLCON0.B.OSCDISCDIS = 0u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);

	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	SCU_TRAPCLR.B.SMUT = 1u;
	SCU_TRAPDIS.B.SMUT = smu_trap;
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);

	return 0;
}

static void wait_clc_enabled(volatile uint32_t *clc)
{
	volatile uint32_t spin = 0u;

	/* DISS = bit 1 */
	while ((*clc) & (1u << 1)) {
		spin++;
		if (spin > 1000000u)
			break;
	}
}

void ulmk_board_i2c0_hw_init(void);

static void bsp_enable_console_clocks(void)
{
	uint16_t pw_cpu;
	uint16_t pw_sfty;

	/*
	 * STM0 CLC: CPU EndInit (iLLD).  ASCLIN0 CLC: also unlock Safety —
	 * with ENIDIS=0 (button/PORST / hot-attach without CBS), a CPU-only
	 * unlock leaves ASCLIN0.CLC.DISR stuck at 1 while STM0 enables fine.
	 */
	pw_cpu  = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[0]);
	pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	MODULE_ASCLIN0.CLC.U = 0u;
	MODULE_STM0.CLC.U = 0u;
	__asm__ volatile("dsync" ::: "memory");
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);

	wait_clc_enabled((volatile uint32_t *)&MODULE_ASCLIN0.CLC.U);
	wait_clc_enabled((volatile uint32_t *)&MODULE_STM0.CLC.U);
}

/*
 * Optional MMIO bring-up for CAN / I2C / ADC / PWM demos.
 * Call from supervisor context only (board_init or future board hook).
 */
void ulmk_board_init_extra_periphs(void)
{
	uint16_t pw_cpu;
	uint16_t pw_sfty;

	pw_cpu  = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[0]);
	pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	MODULE_I2C0.CLC.B.DISR = 0u;
	MODULE_VADC.CLC.B.DISR = 0u;
	MODULE_CAN.CLC.B.DISR = 0u;
	MODULE_CAN.CLC.B.EDIS = 1u;
	MODULE_GTM.CLC.B.DISR = 0u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);

	wait_clc_enabled((volatile uint32_t *)&MODULE_I2C0.CLC.U);
	wait_clc_enabled((volatile uint32_t *)&MODULE_VADC.CLC.U);
	wait_clc_enabled((volatile uint32_t *)&MODULE_CAN.CLC.U);
	wait_clc_enabled((volatile uint32_t *)&MODULE_GTM.CLC.U);

	pw_cpu  = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[0]);
	pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	MODULE_CAN.MCR.B.CLKSEL = 0u;
	MODULE_CAN.MCR.B.CLKSEL = 1u;
	MODULE_CAN.FDR.B.STEP = 1023u;
	MODULE_CAN.FDR.B.DM = 1u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);

	ulmk_board_i2c0_hw_init();
}

/*
 * Full I2C0 kernel bring-up (CLC1, baud, GPCTL.PISEL).
 * Must run in supervisor context before any driver thread touches I2C.
 * Safe to call more than once (idempotent enough for boot).
 */
void ulmk_board_i2c0_hw_init(void)
{
	uint16_t pw_cpu;
	uint16_t pw_sfty;

	pw_cpu  = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[0]);
	pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	MODULE_I2C0.CLC1.B.RMC = 1u;
	while (MODULE_I2C0.CLC1.B.RMC != 1u)
		;
	MODULE_I2C0.CLC1.B.DISR = 0u;
	while (MODULE_I2C0.CLC1.B.DISS == 1u)
		;
	__asm__ volatile("dsync" ::: "memory");
	MODULE_I2C0.ERRIRQSM.U = 0u;
	MODULE_I2C0.PIRQSM.U = 0u;
	MODULE_I2C0.IMSC.U = 0u;
	MODULE_I2C0.RUNCTRL.U = 0u;
	MODULE_I2C0.FDIVCFG.B.INC = 1u;
	MODULE_I2C0.FDIVCFG.B.DEC = 499u;
	MODULE_I2C0.TIMCFG.B.SDA_DEL_HD_DAT = 0x3Fu;
	MODULE_I2C0.TIMCFG.B.FS_SCL_LOW = 1u;
	MODULE_I2C0.TIMCFG.B.EN_SCL_LOW_LEN = 1u;
	MODULE_I2C0.TIMCFG.B.SCL_LOW_LEN = 0x20u;
	MODULE_I2C0.ACCEN0.U = 0xFFFFFFFFu;
	/*
	 * Full-word store — bitfield RMW (ld.w of GPCTL) class-4 trapped on
	 * standalone reset before OpenOCD's reset-end WDT poke.
	 */
	MODULE_I2C0.GPCTL.U = ULMK_BOARD_I2C0_PISEL;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[0], pw_cpu);
}

void ulmk_board_init(void)
{
	/*
	 * Disable WDTs first so PLL lock / ramp waits cannot reset the core.
	 * EndInit still works with DR=1 (same as iLLD disable*Watchdog).
	 *
	 * Do NOT write CBS_OCNTRL (0xF000047C) from CPU code — that register
	 * is an OCDS/DAP unlock used by OpenOCD flash/reset-end.  A CPU store
	 * traps on standalone/button/PORST boot (red ESR0, no console).
	 *
	 * BTV/BIV/ISP/SYSCON are CPU-EndInit protected: arch_init programs
	 * them via ulmk_board_cpu_endinit_{clear,set} so button reset works
	 * without the debugger's CBS unlock.
	 */
	wdt_disable_cpu0();
	wdt_disable_safety();

	/*
	 * TC275 Lite LED3 is wired to ESR0 (active low).  Clear the
	 * Application Reset Indicator so RCU releases the pin if SSW left
	 * ARI set after a prior warm reset.
	 *
	 * Must be a full-word store under Safety EndInit — bitfield RMW on
	 * ESROCFG class-4 traps on button/PORST (same pattern as I2C GPCTL).
	 * With debugger ENIDIS the RMW is silently allowed, which hid this.
	 */
	{
		uint16_t pw_sfty;

		pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();
		IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
		MODULE_SCU.ESROCFG.U = 0x2u; /* ARC=1 → clears ARI */
		IfxScuWdt_setSafetyEndinitInline(pw_sfty);
	}

	(void)pll_init_20mhz_200mhz();
	bsp_enable_console_clocks();
	ulmk_board_init_extra_periphs();
}

/*
 * CPU EndInit for the local core only.  Unlocking WDTCPU[n] from another
 * core spins forever on ENDINIT — secondaries hit this path from
 * ulmk_arch_irq_vectors_init / mpu_init.
 */
void ulmk_board_cpu_endinit_clear(void)
{
	uint16_t pw;
	uint32_t id;

	__asm__ volatile("mfcr %0, %1" : "=d"(id) : "i"(0xFE1Cu));
	id &= 7u;
	if (id > 2u)
		return;
	pw = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[id]);
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[id], pw);
}

void ulmk_board_cpu_endinit_set(void)
{
	uint16_t pw;
	uint32_t id;

	__asm__ volatile("mfcr %0, %1" : "=d"(id) : "i"(0xFE1Cu));
	id &= 7u;
	if (id > 2u)
		return;
	pw = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[id]);
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[id], pw);
}

/*
 * Disable this core's CPU WDT.  Must run on the target core — unlocking
 * WDTCPU[n] EndInit from another core spins forever on ENDINIT.
 * Idempotent: skip if DR already set (OCDS hil.cfg may disarm first).
 */
void ulmk_board_cpu_wdt_disable_self(void)
{
	uint16_t pw;
	uint32_t id;
	uint32_t spins;

	__asm__ volatile("mfcr %0, %1" : "=d"(id) : "i"(0xFE1Cu));
	id &= 7u;
	if (id > 2u)
		return;
	if (MODULE_SCU.WDTCPU[id].CON1.B.DR != 0u)
		return;

	pw = IfxScuWdt_getCpuWatchdogPasswordInline(&MODULE_SCU.WDTCPU[id]);
	IfxScuWdt_clearCpuEndinitInline(&MODULE_SCU.WDTCPU[id], pw);
	MODULE_SCU.WDTCPU[id].CON1.B.DR = 1u;
	IfxScuWdt_setCpuEndinitInline(&MODULE_SCU.WDTCPU[id], pw);

	/* Bound any stuck EndInit poll if debugger left SFRs inconsistent. */
	for (spins = 0u; spins < 10000u; spins++) {
		if (MODULE_SCU.WDTCPU[id].CON0.B.ENDINIT != 0u)
			break;
	}
}

/*
 * Start CPU1 — Infineon PC is only writable while the core is debug-halted.
 * Sequence (iLLD IfxCpu_startCore + explicit halt-before-PC):
 *   1. DBGSR.HALT = 1 (halt) so PC accepts the write
 *   2. program PC
 *   3. PMCSR[1].REQSLP = Run under Safety EndInit
 *   4. DBGSR.HALT = 2 (run)
 * Do NOT touch WDTCPU[1] EndInit from CPU0 — that wait loops forever.
 */
void ulmk_board_cpu_start(uint32_t cpu_id, void (*entry)(void))
{
	volatile uint32_t *pc;
	volatile uint32_t *dbgsr;
	uint16_t           pw_sfty;
	uint32_t           v;
	uint32_t           spins;

	if (cpu_id != 1u || !entry)
		return;

	pc    = (volatile uint32_t *)(uintptr_t)ULMK_BOARD_CPU1_PC;
	dbgsr = (volatile uint32_t *)(uintptr_t)ULMK_BOARD_CPU1_DBGSR;

	/* Halt so PC is writable (HALT field = 1). */
	v = *dbgsr;
	v = (v & ~0x6u) | (1u << 1);
	*dbgsr = v;
	for (spins = 0u; spins < 10000u; spins++) {
		if (((*dbgsr) & 0x6u) == (1u << 1))
			break;
	}

	/*
	 * PC.B.PC occupies bits [31:1].  iLLD writes B.PC = entry>>1 which
	 * packs to the same word as (entry & ~1).
	 */
	*pc = (uint32_t)(uintptr_t)entry & ~1u;
	__asm__ volatile("dsync" ::: "memory");

	pw_sfty = IfxScuWdt_getSafetyWatchdogPasswordInline();
	IfxScuWdt_clearSafetyEndinitInline(pw_sfty);
	/* REQSLP = 0 → Run (IfxScu_PMCSR_REQSLP_Run); PMCSR[1] = CPU1 */
	MODULE_SCU.PMCSR[1].B.REQSLP = 0u;
	IfxScuWdt_setSafetyEndinitInline(pw_sfty);

	v = *dbgsr;
	v = (v & ~0x6u) | (ULMK_BOARD_DBGSR_HALT_RUN << 1);
	*dbgsr = v;
}
