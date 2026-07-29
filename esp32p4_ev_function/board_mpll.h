/* SPDX-License-Identifier: MIT */
#ifndef BOARD_MPLL_H
#define BOARD_MPLL_H

/* Enable MSPI PHY + MPLL @ 400 MHz (early board_init, M-mode). */
int board_mpll_enable_400m(void);

#endif /* BOARD_MPLL_H */
