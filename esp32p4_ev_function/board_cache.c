/* SPDX-License-Identifier: MIT */
/*
 * ESP32-P4 D-cache range maintenance.
 *
 * Programming the sync engine takes four register writes (map, address, size,
 * trigger) that hardware does not latch atomically: a thread preempted midway
 * would have its address spliced with the next one's size.  The primitives
 * therefore run in M-mode behind the ulmk_dcache_* syscalls, with interrupts
 * masked across each sequence.  This also replaces the ROM Cache_* helpers,
 * which are not reentrant and faulted when two threads entered them at once.
 *
 * L1 and L2 are synced as separate operations, in that order: a writeback
 * pushes dirty L1 lines only as far as L2, so folding both into one SYNC_MAP
 * leaves DMA reading stale PSRAM while the CPU sees its own cached writes.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk_arch.h>
#include "board_cache.h"

#define CACHE_BASE		0x3ff10000u
#define CACHE_SYNC_CTRL		(CACHE_BASE + 0x98u)
#define CACHE_SYNC_MAP		(CACHE_BASE + 0x9cu)
#define CACHE_SYNC_ADDR		(CACHE_BASE + 0xa0u)
#define CACHE_SYNC_SIZE		(CACHE_BASE + 0xa4u)

#define SYNC_INVALIDATE_ENA	(1u << 0)
#define SYNC_WRITEBACK_ENA	(1u << 2)

/* SYNC_MAP: [4] = L1-DCache, [5] = L2-Cache. */
#define SYNC_MAP_L1_DCACHE	(1u << 4)
#define SYNC_MAP_L2_CACHE	(1u << 5)

#define CACHE_LINE		64u
/*
 * SYNC_SIZE is 28 bits, so the split is not a hardware limit: it bounds how
 * long interrupts stay masked, since a full framebuffer writeback is
 * milliseconds of PSRAM traffic.
 */
#define SYNC_CHUNK		0x1000u	/* 4 KiB ≈ tens of µs with IRQs off */
/* Hardware handshake; bounded so a wedged sync cannot hang the kernel. */
#define SYNC_SPIN_LIMIT		1000000u

static inline void wr(uintptr_t reg, uint32_t v)
{
	*(volatile uint32_t *)reg = v;
}

static inline uint32_t rd(uintptr_t reg)
{
	return *(volatile uint32_t *)reg;
}

static void sync_op(uint32_t map, uint32_t trigger, uintptr_t a, uint32_t len)
{
	uint32_t spins;

	wr(CACHE_SYNC_MAP, map);
	wr(CACHE_SYNC_ADDR, (uint32_t)a);
	wr(CACHE_SYNC_SIZE, len);
	wr(CACHE_SYNC_CTRL, trigger);

	/* Hardware clears the trigger bit when the operation retires. */
	for (spins = 0u; spins < SYNC_SPIN_LIMIT; spins++) {
		if ((rd(CACHE_SYNC_CTRL) & trigger) == 0u)
			return;
	}
}

static void sync_range(uint32_t trigger, void *addr, size_t size)
{
	uintptr_t a = (uintptr_t)addr;
	uintptr_t end;

	if (a == 0u || size == 0u)
		return;

	end = (a + size + CACHE_LINE - 1u) & ~(uintptr_t)(CACHE_LINE - 1u);
	a &= ~(uintptr_t)(CACHE_LINE - 1u);

	while (a < end) {
		uint32_t n = (uint32_t)(end - a);
		ulmk_arch_irq_key_t key;

		if (n > SYNC_CHUNK)
			n = SYNC_CHUNK;

		key = ulmk_arch_cpu_irq_save();
		sync_op(SYNC_MAP_L1_DCACHE, trigger, a, n);
		sync_op(SYNC_MAP_L2_CACHE, trigger, a, n);
		ulmk_arch_cpu_irq_restore(key);

		a += n;
	}
}

void ulmk_board_dcache_clean(void *addr, size_t len)
{
	__asm__ volatile("fence rw, rw" ::: "memory");
	sync_range(SYNC_WRITEBACK_ENA, addr, len);
}

void ulmk_board_dcache_invalidate(void *addr, size_t len)
{
	sync_range(SYNC_INVALIDATE_ENA, addr, len);
	__asm__ volatile("fence rw, rw" ::: "memory");
}

void board_dcache_clean(const void *addr, size_t size)
{
	(void)ulmk_dcache_clean(addr, size);
}

void board_dcache_invalidate(const void *addr, size_t size)
{
	(void)ulmk_dcache_invalidate(addr, size);
}

void board_dcache_clean_invalidate(const void *addr, size_t size)
{
	(void)ulmk_dcache_clean_invalidate(addr, size);
}

/*
 * Coalesce a partial rectangle into the full-width strip covering it: one
 * syscall instead of h, at the cost of writing back the untouched pixels
 * on either side.  The syscall overhead dominates for the tall, narrow
 * rectangles LVGL DIRECT produces.
 */
static void area_op(void (*op)(const void *, size_t), void *buf,
		    uint32_t stride, int32_t x, int32_t y, int32_t w,
		    int32_t h, uint32_t px_size)
{
	uint8_t *row;

	(void)x;
	(void)px_size;

	if (!buf || !op || w <= 0 || h <= 0 || stride == 0u)
		return;

	row = (uint8_t *)buf + (uint32_t)y * stride;
	op(row, (size_t)h * (size_t)stride);
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
