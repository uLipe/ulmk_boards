/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CACHE_H
#define BOARD_CACHE_H

#include <stddef.h>

/*
 * clean      — after CPU wrote via the *cached* PSRAM alias (0x4800_0000).
 * invalidate — before reading data a DMA engine produced.
 *
 * These are the U-mode driver API and go through the ulmk_dcache_*
 * syscalls; the sync engine itself is driven in M-mode.  Code already in
 * M-mode (ISRs, arch hooks) must call ulmk_board_dcache_* directly.
 */
void board_dcache_clean(const void *addr, size_t size);
void board_dcache_invalidate(const void *addr, size_t size);

#endif /* BOARD_CACHE_H */
