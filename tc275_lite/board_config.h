/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_config.h — SoC / platform constants for TC275 Lite Kit.
 *
 * Single-core bring-up: CPU0 only (no SMP).  Console is ASCLIN0 on P14.0/P14.1
 * via the onboard USB–UART bridge (userspace driver thread, not kernel printk).
 */

#ifndef ULMK_BOARD_CONFIG_H
#define ULMK_BOARD_CONFIG_H

/* ── Clocks (20 MHz crystal → 200 MHz PLL in board_init) ───────────────── */

#define ULMK_BOARD_FOSC_HZ		20000000u
#define ULMK_BOARD_FCPU_HZ		200000000u
/*
 * ASCLIN CSR=ascFastClock → fBaud2 = fMAX / CCUCON0.BAUD2DIV.
 * iLLD 200 MHz profile sets BAUD2DIV=1 → 200 MHz (not fSPB).
 */
#define ULMK_BOARD_FA_HZ		200000000u

/* ── IRQ: AURIX Service Request (SRC) block — TC27x layout ───────────── */

#define ULMK_BOARD_SRC_BASE		0xF0038000u
/* TC27D SRC.SRCR.SRE is bit 10 (TOS starts at bit 11). */
#define ULMK_BOARD_SRC_SRE_BIT		10u
#define ULMK_BOARD_SRC_STM0_SR0		0xF0038490u
#define ULMK_BOARD_SRC_ASCLIN0_TX	0xF0038080u
#define ULMK_BOARD_SRC_ASCLIN0_RX	0xF0038084u
#define ULMK_BOARD_SRC_VADC_G0_SR0	0xF0038980u

/* SRPN allocation (CPU0) — one priority per service request line. */
#define ULMK_BOARD_IRQ_STM0		2u
#define ULMK_BOARD_IRQ_ASCLIN0_TX	3u
#define ULMK_BOARD_IRQ_ASCLIN0_RX	4u
#define ULMK_BOARD_IRQ_VADC_G0		5u

/* ── Timer peripheral (STM0, Core 0) — IfxStm_reg.h MODULE_STM0 ─────────── */

#define ULMK_BOARD_STM0_BASE		0xF0000000u
/* fSTM follows fSPB; with PLL@200 MHz and SPB div=2 → 100 MHz. */
#define ULMK_BOARD_FSTM_HZ		100000000u

/* ── ASCLIN0 (USB virtual COM on Lite Kit) ─────────────────────────────── */

#define ULMK_BOARD_ASCLIN0_BASE		0xF0000600u
#define ULMK_BOARD_CONSOLE_BAUD		115200u

/* ── Lite Kit peripherals (thin userspace drivers) ─────────────────────── */

#define ULMK_BOARD_VADC_BASE		0xF0020000u
/* Lite Kit pot on AN0 → VADC G0CH0; VAREF = VDDM = 3.3 V. */
#define ULMK_BOARD_VADC_VAREF_MV	3300u
#define ULMK_BOARD_ADC_POT_GROUP	0u
#define ULMK_BOARD_ADC_POT_CHANNEL	0u
#define ULMK_BOARD_I2C0_BASE		0xF00C0000u
#define ULMK_BOARD_CAN_BASE		0xF0018000u
#define ULMK_BOARD_GTM_BASE		0xF0100000u

/* LED1 P00.5, LED2 P00.6 (active-low); Button1 P00.7; pot AN0. */
#define ULMK_BOARD_LED1_PORT		0u
#define ULMK_BOARD_LED1_PIN		5u
#define ULMK_BOARD_LED2_PORT		0u
#define ULMK_BOARD_LED2_PIN		6u
#define ULMK_BOARD_BUTTON_PORT		0u
#define ULMK_BOARD_BUTTON_PIN		7u

/* ── Memory map (MPU coarse regions; must match memory.ld ORIGIN) ──────── */

#define ULMK_BOARD_FLASH_BASE		0x80000000u
#define ULMK_BOARD_FLASH_SIZE		0x00400000u
#define ULMK_BOARD_RAM_BASE		0x70000000u
#define ULMK_BOARD_RAM_SIZE		0x0001C000u	/* CPU0 DSPR 112 KB (TC1.6E) */
#define ULMK_BOARD_PERIPH_BASE		0xF0000000u
#define ULMK_BOARD_PERIPH_SIZE		0x10000000u

/* No QEMU virt console — kernel printk is a no-op on this board. */
#define ULMK_BOARD_HAVE_VIRT_CONSOLE	0

/* ── Arch / kernel quirks ──────────────────────────────────────────────── */

#define ULMK_BOARD_IDLE_IS_WAIT		1
#define ULMK_BOARD_MPU_NUM_DPR		18
#define ULMK_BOARD_MPU_NUM_CPR		10

/*
 * TriCore ISA: TC275 CPU0 is TC1.6E → 1.6.1.  QEMU tc3xx boards use 1.6.2.
 * Arch code may branch on these when a silicon difference appears.
 */
#define ULMK_BOARD_TRICORE_ISA_MAJOR	1
#define ULMK_BOARD_TRICORE_ISA_MINOR	6
#define ULMK_BOARD_TRICORE_ISA_PATCH	1

/* HIL boot milestones — g_ulmk_board_hil_scratch in .user_bss (nm the ELF). */

#endif /* ULMK_BOARD_CONFIG_H */
