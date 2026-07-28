/* SPDX-License-Identifier: MIT */
/*
 * board_cache.c — userspace D-cache maintenance for drivers / LVGL.
 */
#include <stddef.h>
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_cache.h"

#define DCACHE_LINE	32u

static void clean_range(const void *addr, size_t size)
{
	uintptr_t start;
	uintptr_t end;

	if (!addr || size == 0u)
		return;

	start = (uintptr_t)addr & ~(uintptr_t)(DCACHE_LINE - 1u);
	end = ((uintptr_t)addr + size + (DCACHE_LINE - 1u)) &
	      ~(uintptr_t)(DCACHE_LINE - 1u);
	(void)ulmk_dcache_clean((void *)start, end - start);
}

void board_dcache_clean(const void *addr, size_t size)
{
	clean_range(addr, size);
}

void board_dcache_clean_all(void)
{
	/* size 0 → arch cleans the whole D-cache (set/way). */
	(void)ulmk_dcache_clean(NULL, 0u);
}

void board_dcache_invalidate(const void *addr, size_t size)
{
	uintptr_t start;
	uintptr_t end;

	if (!addr || size == 0u)
		return;

	start = (uintptr_t)addr & ~(uintptr_t)(DCACHE_LINE - 1u);
	end = ((uintptr_t)addr + size + (DCACHE_LINE - 1u)) &
	      ~(uintptr_t)(DCACHE_LINE - 1u);
	(void)ulmk_dcache_invalidate((void *)start, end - start);
}

void board_dcache_clean_invalidate(const void *addr, size_t size)
{
	uintptr_t start;
	uintptr_t end;

	if (!addr || size == 0u)
		return;

	start = (uintptr_t)addr & ~(uintptr_t)(DCACHE_LINE - 1u);
	end = ((uintptr_t)addr + size + (DCACHE_LINE - 1u)) &
	      ~(uintptr_t)(DCACHE_LINE - 1u);
	(void)ulmk_dcache_clean_invalidate((void *)start, end - start);
}

static void area_op(void (*op)(const void *, size_t), void *buf,
		    uint32_t stride, int32_t x, int32_t y, int32_t w,
		    int32_t h, uint32_t px_size)
{
	uint8_t *row;
	int32_t i;
	size_t nbytes;

	if (!buf || !op || w <= 0 || h <= 0 || px_size == 0u || stride == 0u)
		return;

	nbytes = (size_t)w * (size_t)px_size;

	/* Full-width strip → one contiguous range (typical LVGL DIRECT). */
	if (x == 0 && nbytes == (size_t)stride) {
		row = (uint8_t *)buf + (uint32_t)y * stride;
		op(row, (size_t)h * (size_t)stride);
		return;
	}

	row = (uint8_t *)buf + (uint32_t)y * stride +
	      (uint32_t)x * px_size;
	for (i = 0; i < h; i++) {
		op(row, nbytes);
		row += stride;
	}
}

void board_dcache_clean_area(void *buf, uint32_t stride,
			     int32_t x, int32_t y, int32_t w, int32_t h,
			     uint32_t px_size)
{
	area_op(board_dcache_clean, buf, stride, x, y, w, h, px_size);
}

void board_dcache_clean_invalidate_area(void *buf, uint32_t stride,
					int32_t x, int32_t y,
					int32_t w, int32_t h,
					uint32_t px_size)
{
	area_op(board_dcache_clean_invalidate, buf, stride, x, y, w, h,
		px_size);
}
