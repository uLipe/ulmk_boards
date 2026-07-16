/* SPDX-License-Identifier: MIT */
#ifndef ULMK_BOARD_SMP_H
#define ULMK_BOARD_SMP_H

#include <stdint.h>

/*
 * Release a halted secondary CPU (TC275: CPU1).  Sets PC then
 * DBGSR.HALT=2 (iLLD IfxCpu_startCore).  Caller/secondary must disable
 * its own WDT via ulmk_board_cpu_wdt_disable_self().
 */
void ulmk_board_cpu_start(uint32_t cpu_id, void (*entry)(void));

/* Run on the local core only — unlocks that core's WDTCPU EndInit. */
void ulmk_board_cpu_wdt_disable_self(void);

/* CAN/I2C/VADC/GTM CLC + CAN FDR + I2C0 GPCTL (supervisor only). */
void ulmk_board_init_extra_periphs(void);

/* I2C0 CLC1/baud/GPCTL — supervisor only; call before i2c server runs. */
void ulmk_board_i2c0_hw_init(void);

#endif /* ULMK_BOARD_SMP_H */
