/* SPDX-License-Identifier: MIT */
#ifndef BOARD_SDRAM_H
#define BOARD_SDRAM_H

#include "board_config.h"

/*
 * Userspace helpers.  Call only after ulmk_mem_map() of
 * ULMK_BOARD_SDRAM_BASE (init itself runs in ulmk_board_init).
 */

/* Write/read a magic pattern at ULMK_BOARD_SDRAM_BASE.  Returns 0 on OK. */
int board_sdram_probe(void);

/* 1 if probe succeeds (SDRAM reachable), else 0. */
int board_sdram_ready(void);

#endif /* BOARD_SDRAM_H */
