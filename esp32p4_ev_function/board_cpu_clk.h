/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CPU_CLK_H
#define BOARD_CPU_CLK_H

/*
 * Raise HP root clock from the bootloader rate (~90 MHz on CPLL/4) to
 * CPLL @ 400 MHz / 1.  Call after .bss is clear, before PSRAM / drivers.
 */
int board_cpu_clk_set_400m(void);

#endif /* BOARD_CPU_CLK_H */
