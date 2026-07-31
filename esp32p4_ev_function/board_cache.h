/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CACHE_H
#define BOARD_CACHE_H

#include <stddef.h>
#include <stdint.h>

/*
 * clean      — after CPU wrote via the *cached* PSRAM alias (0x4800_0000).
 * invalidate — before reading data a DMA engine produced.
 *
 * These are the U-mode driver API and go through the ulmk_dcache_*
 * syscalls; the sync engine itself is driven in M-mode.  Code already in
 * M-mode (ISRs, arch hooks) must call ulmk_board_dcache_* directly.
 *
 * Range ops expand to D-cache line boundaries (64 B on ESP32-P4).
 */
void board_dcache_clean(const void *addr, size_t size);
void board_dcache_invalidate(const void *addr, size_t size);
void board_dcache_clean_invalidate(const void *addr, size_t size);

/*
 * Clean a rectangle inside a strided buffer (e.g. RGB565 FB).
 * @stride  bytes per row; @px_size bytes per pixel.
 */
void board_dcache_clean_area(void *buf, uint32_t stride,
			     int32_t x, int32_t y, int32_t w, int32_t h,
			     uint32_t px_size);

/* Same geometry; clean+invalidate — used before DW_GDMA/DPI scanout. */
void board_dcache_clean_invalidate_area(void *buf, uint32_t stride,
					int32_t x, int32_t y,
					int32_t w, int32_t h,
					uint32_t px_size);

#endif /* BOARD_CACHE_H */
