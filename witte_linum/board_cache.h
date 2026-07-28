/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CACHE_H
#define BOARD_CACHE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Board cache services — thin wrappers over ulmk_dcache_* (DRIVER syscall).
 * Enable itself runs in arch_init when ULMK_BOARD_ENABLE_CPU_CACHE=1.
 *
 * Range cleans expand to D-cache line boundaries (32 B on M7).
 */
void board_dcache_clean(const void *addr, size_t size);
void board_dcache_clean_all(void);
void board_dcache_invalidate(const void *addr, size_t size);
void board_dcache_clean_invalidate(const void *addr, size_t size);

/*
 * Clean a rectangle inside a strided buffer (e.g. RGB565 FB).
 * @stride  bytes per row; @px_size bytes per pixel.
 */
void board_dcache_clean_area(void *buf, uint32_t stride,
			     int32_t x, int32_t y, int32_t w, int32_t h,
			     uint32_t px_size);

/* Same geometry; clean+invalidate (CMSIS "flush") — used before LTDC/DMA. */
void board_dcache_clean_invalidate_area(void *buf, uint32_t stride,
					int32_t x, int32_t y,
					int32_t w, int32_t h,
					uint32_t px_size);

#endif /* BOARD_CACHE_H */
