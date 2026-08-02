/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CPU_CLK_H
#define BOARD_CPU_CLK_H

#include <stdint.h>

/*
 * Raise HP root clock from the bootloader rate (~90 MHz on CPLL/4) to
 * CPLL @ 400 MHz / 1.  Call after .bss is clear, before PSRAM / drivers.
 */
int board_cpu_clk_set_400m(void);

/* Park/release the other hart across a clock switch (see board_cpu_clk.c). */
void board_cpu_peer_stall(uint32_t peer);
void board_cpu_peer_unstall(uint32_t peer);

#endif /* BOARD_CPU_CLK_H */
