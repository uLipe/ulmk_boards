/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_config.h — SoC / platform constants for TC275 Lite Kit.
 *
 * Single-core bring-up: CPU0 only (no SMP).  Console is ASCLIN0 on P14.0/P14.1
 * via the onboard USB–UART bridge (userspace driver thread, not kernel printk).
 */

#ifndef ULMK_BOARD_CONFIG_H
#define ULMK_BOARD_CONFIG_H

/*
 * SMP targets CPU0 + CPU1 (symmetric 1.6.1).  CPU2 (1.6e) is out of scope.
 * UP builds still compile with NUM_CPU=2 but only CPU0 is started unless
 * ULMK_CONFIG_ENABLE_SMP=1.
 */
#ifndef ULMK_ARCH_NUM_CPU
#define ULMK_ARCH_NUM_CPU		2
#endif

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
#define ULMK_BOARD_SRC_STM0_SR1		0xF0038494u
#define ULMK_BOARD_SRC_ASCLIN0_TX	0xF0038080u
#define ULMK_BOARD_SRC_ASCLIN0_RX	0xF0038084u
#define ULMK_BOARD_SRC_VADC_G0_SR0	0xF0038980u
#define ULMK_BOARD_SRC_CAN0_INT0	0xF0038900u
#define ULMK_BOARD_SRC_I2C0_P		0xF0038314u
/* SCU ERU OGU0 → SRC_SCUERU0 (gpio_subscribe edge IRQ) */
#define ULMK_BOARD_SRC_SCU_ERU0		0xF0038CD4u
/*
 * GPSR soft IRQ — one SR0 node per core group (iLLD SRC_GPSR00/10/20).
 * Program TOS to that same CPU; SETR triggers the IPI.
 */
#define ULMK_BOARD_SRC_GPSR00		0xF0039000u	/* GPSR0 group → CPU0 */
#define ULMK_BOARD_SRC_GPSR10		0xF0039020u	/* GPSR1 group → CPU1 */
#define ULMK_BOARD_SRC_GPSR20		0xF0039040u	/* GPSR2 group → CPU2 */

/*
 * CPU1 (MODULE_CPU1 @ 0xF8830000) program counter + debug halt — iLLD TC27D
 * IfxCpu_reg.h.  Write PC.B.PC = entry>>1, then DBGSR.HALT = 2 to run.
 */
#define ULMK_BOARD_CPU1_PC		0xF883FE08u
#define ULMK_BOARD_CPU1_DBGSR		0xF883FD00u
#define ULMK_BOARD_DBGSR_HALT_RUN	0x2u	/* write to HALT[1:0] */

/* SRPN allocation (CPU0) — one priority per service request line. */
#define ULMK_BOARD_IRQ_STM0		2u
#define ULMK_BOARD_IRQ_ASCLIN0_TX	3u
#define ULMK_BOARD_IRQ_ASCLIN0_RX	4u
#define ULMK_BOARD_IRQ_VADC_G0		5u
#define ULMK_BOARD_IRQ_CAN0		6u
#define ULMK_BOARD_IRQ_I2C0_P		7u
#define ULMK_BOARD_IRQ_IPI		8u	/* GPSR soft resched */
#define ULMK_BOARD_IRQ_GPIO_ERU		9u	/* SCU ERU0 */

/* ── Timer peripheral (STM0, Core 0) — IfxStm_reg.h MODULE_STM0 ─────────── */

#define ULMK_BOARD_STM0_BASE		0xF0000000u
/* fSTM follows fSPB; with PLL@200 MHz and SPB div=2 → 100 MHz. */
#define ULMK_BOARD_FSTM_HZ		100000000u

/* ── Driver instance limits (n = 0 .. MAX-1) ───────────────────────────── */

#define ULMK_BOARD_PINMUX_MAX		1u
#define ULMK_BOARD_GPIO_MAX		1u
#define ULMK_BOARD_ASCLIN_MAX		1u	/* ASCLIN0 console */
#define ULMK_BOARD_I2C_MAX		1u	/* I2C0 */
#define ULMK_BOARD_CAN_MAX		1u	/* MultiCAN node 0 */
#define ULMK_BOARD_ADC_MAX		1u	/* logical channels (pot) */
#define ULMK_BOARD_PWM_MAX		2u	/* TOM channels → LED1/LED2 */

/* ── ASCLIN0 (USB virtual COM on Lite Kit) ─────────────────────────────── */

#define ULMK_BOARD_ASCLIN0_BASE		0xF0000600u
#define ULMK_BOARD_CONSOLE_BAUD		115200u
#define ULMK_BOARD_ASCLIN0_TX_PORT	14u
#define ULMK_BOARD_ASCLIN0_TX_PIN	0u
#define ULMK_BOARD_ASCLIN0_TX_ALT	2u
#define ULMK_BOARD_ASCLIN0_RX_PORT	14u
#define ULMK_BOARD_ASCLIN0_RX_PIN	1u
#define ULMK_BOARD_ASCLIN0_RX_ALTI	0u

/* ── Lite Kit peripherals (thin userspace drivers) ─────────────────────── */

#define ULMK_BOARD_VADC_BASE		0xF0020000u
/* Lite Kit pot on AN0 → VADC G0CH0; VAREF = VDDM = 3.3 V. */
#define ULMK_BOARD_VADC_VAREF_MV	3300u
#define ULMK_BOARD_ADC0_GROUP		0u
#define ULMK_BOARD_ADC0_CHANNEL		0u
/* Compat aliases for older demos. */
#define ULMK_BOARD_ADC_POT_GROUP	ULMK_BOARD_ADC0_GROUP
#define ULMK_BOARD_ADC_POT_CHANNEL	ULMK_BOARD_ADC0_CHANNEL

#define ULMK_BOARD_I2C0_BASE		0xF00C0000u
/*
 * Lite Kit I2C0 (X1 header + Shield2Go/Arduino share the same module):
 *   SCL  P15.4  alt6  PISEL=c   (also P13.1 on shield)
 *   SDA  P15.5  alt6  PISEL=c   (also P13.2 on shield)
 * Needs external pull-ups on the bus (shield modules usually provide them).
 */
#define ULMK_BOARD_I2C0_SCL_PORT	15u
#define ULMK_BOARD_I2C0_SCL_PIN		4u
#define ULMK_BOARD_I2C0_SCL_ALT		6u
#define ULMK_BOARD_I2C0_SDA_PORT	15u
#define ULMK_BOARD_I2C0_SDA_PIN		5u
#define ULMK_BOARD_I2C0_SDA_ALT		6u
#define ULMK_BOARD_I2C0_PISEL		2u	/* IfxI2c_PinSelect_c */
#define ULMK_BOARD_I2C0_BITRATE_HZ	100000u
#define ULMK_BOARD_CAN_BASE		0xF0018000u
/*
 * Lite Kit TLE9251V ↔ MultiCAN node 0:
 *   TX  P20.8  TXDCAN0  (alt5)
 *   RX  P20.7  RXDCAN0B (RXSEL=b)
 *   #NEN P20.6  GPIO out low → transceiver normal mode
 */
#define ULMK_BOARD_CAN_TX_PORT		20u
#define ULMK_BOARD_CAN_TX_PIN		8u
#define ULMK_BOARD_CAN_TX_ALT		5u
#define ULMK_BOARD_CAN_RX_PORT		20u
#define ULMK_BOARD_CAN_RX_PIN		7u
#define ULMK_BOARD_CAN_RX_ALTI		1u	/* RXSEL = b */
#define ULMK_BOARD_CAN_NEN_PORT		20u
#define ULMK_BOARD_CAN_NEN_PIN		6u
#define ULMK_BOARD_GTM_BASE		0xF0100000u
/* GTM cluster clock follows fSPB after board_init PLL (200 MHz / 2). */
#define ULMK_BOARD_FGTM_HZ		100000000u
#define ULMK_BOARD_GTM_TOM0_BASE	0xF0108000u
#define ULMK_BOARD_GTM_CMU_BASE		0xF0100300u
#define ULMK_BOARD_GTM_TOUTSEL_BASE	0xF019FD30u

/*
 * Lite Kit LEDs ← GTM TOM0 (active-low, port alt1):
 *   PWM0 / LED1 P00.5 ← TOM0_CH12 / TOUT14 / ToutSel_a
 *   PWM1 / LED2 P00.6 ← TOM0_CH13 / TOUT15 / ToutSel_a
 */
#define ULMK_BOARD_PWM0_PORT		0u
#define ULMK_BOARD_PWM0_PIN		5u
#define ULMK_BOARD_PWM0_ALT		1u
#define ULMK_BOARD_PWM0_TOM_CH		12u
#define ULMK_BOARD_PWM0_TOUT		14u
#define ULMK_BOARD_PWM1_PORT		0u
#define ULMK_BOARD_PWM1_PIN		6u
#define ULMK_BOARD_PWM1_ALT		1u
#define ULMK_BOARD_PWM1_TOM_CH		13u
#define ULMK_BOARD_PWM1_TOUT		15u

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

/*
 * UP: WAIT.  SMP GPSR IPI + WAIT quirk is handled in arch/tricore
 * (ulmk_arch_cpu_idle) — board_config must not #if on kernel config
 * (platform.h is a raw snapshot; ENABLE_SMP is not visible here alone).
 */
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
