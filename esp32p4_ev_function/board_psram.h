/* SPDX-License-Identifier: MIT */
#ifndef BOARD_PSRAM_H
#define BOARD_PSRAM_H
#include <stddef.h>
int board_psram_init(void);
/* Optional — may hang on untuned MSPI; not called from board_services. */
int board_psram_enable_axi(void);
void *board_psram_base(void);
size_t board_psram_size(void);
int board_psram_ready(void);
/* Non-cache alias of a PSRAM pointer (same phys); NULL if not in window. */
void *board_psram_nc(void *cached);
#endif
