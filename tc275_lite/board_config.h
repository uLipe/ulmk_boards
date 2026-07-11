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
#define ULMK_BOARD_FA_HZ		100000000u	/* fCPU / 2 — ASCLIN fA */

/* ── IRQ: AURIX Service Request (SRC) block — TC27x layout ───────────── */

#define ULMK_BOARD_SRC_BASE		0xF0038000u
#define ULMK_BOARD_SRC_SRE_BIT		12u
#define ULMK_BOARD_SRC_STM0_SR0		0xF0038490u

/* ── Timer peripheral (STM0, Core 0) ───────────────────────────────────── */

#define ULMK_BOARD_STM0_BASE		0xF0001000u

/* ── ASCLIN0 (USB virtual COM on Lite Kit) ─────────────────────────────── */

#define ULMK_BOARD_ASCLIN0_BASE		0xF0000600u
#define ULMK_BOARD_CONSOLE_BAUD		115200u

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
