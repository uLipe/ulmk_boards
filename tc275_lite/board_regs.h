/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_regs.h — SCU / flash register offsets for board_init (CPU0).
 */

#ifndef BOARD_REGS_H
#define BOARD_REGS_H

#include <stdint.h>

#define REG32(a)	(*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a)	(*(volatile uint16_t *)(uintptr_t)(a))

#define SCU_BASE		0xF0036000u

#define SCU_OSCCON		(SCU_BASE + 0x10u)
#define SCU_PLLCON0		(SCU_BASE + 0x18u)
#define SCU_PLLCON1		(SCU_BASE + 0x1Cu)
#define SCU_CCUCON0		(SCU_BASE + 0x60u)
#define SCU_CCUCON1		(SCU_BASE + 0x64u)
#define SCU_SYSPLLSTAT		(SCU_BASE + 0x14u)

#define SCU_WDTCPU0_CON0	(SCU_BASE + 0x100u)
#define SCU_WDTCPU0_CON1	(SCU_BASE + 0x104u)
#define SCU_WDTS_CON0		(SCU_BASE + 0xF0u)
#define SCU_WDTS_CON1		(SCU_BASE + 0xF4u)

/* PFlash FCON — TC27x PMU0 (iLLD FLASH0_FCON @ 0xF8002014). */
#define FLASH_FCON		0xF8002014u

#endif /* BOARD_REGS_H */
