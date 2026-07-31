/* SPDX-License-Identifier: MIT */
/*
 * PSRAM stress with the DPI scanout live.
 *
 * A quiet march over free PSRAM already PASSed; the LVGL tlsf pool still
 * corrupts under render+flush.  This version reproduces the hot path:
 *   - GDMA reading the dual FBs (already streaming when we are called)
 *   - CPU dirtying and writebacking FB-sized strips (what flush_cb does)
 *   - interleaved readback of a patterned region after clean+invalidate
 *
 * Fail here → bus/timing (DQS).  Pass here → look at LVGL/heap software.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <display.h>
#include "board_config.h"
#include "board_cache.h"
#include "board_console.h"
#include "board_timer.h"
#include "board_psram_tune.h"
#include "psram_stress.h"

#define GUARD_BASE	(ULMK_BOARD_PSRAM_BASE + ULMK_BOARD_DISPLAY_FB_MAP_SIZE)
#define GUARD_BYTES	ULMK_BOARD_LVGL_HEAP_GUARD
#define STRESS_BASE	(ULMK_BOARD_LVGL_HEAP_ADDR + ULMK_BOARD_LVGL_HEAP_SIZE)
#define STRESS_BYTES	(ULMK_BOARD_PSRAM_BASE + ULMK_BOARD_PSRAM_SIZE - \
			 STRESS_BASE)
#define PASSES		8u
#define FB_BYTES	ULMK_BOARD_DISPLAY_FB_BYTES
#define FB_STRIDE	(ULMK_BOARD_DISPLAY_W * ULMK_BOARD_DISPLAY_BPP)

static uint32_t pattern(uint32_t pass, uint32_t idx)
{
	return 0xA5000000u ^ (pass * 0x11111111u) ^ (idx * 0x00010001u) ^
	       (idx << 16);
}

static uint32_t guard_pat(uint32_t idx)
{
	return 0xCAFE0000u ^ idx ^ (idx << 16);
}

static int check_words(volatile uint32_t *p, uint32_t nwords, uint32_t pass,
		       const char *tag)
{
	uint32_t i;
	uint32_t expect;
	uint32_t got;

	for (i = 0u; i < nwords; i++) {
		expect = pattern(pass, i);
		got = p[i];
		if (got != expect) {
			board_console_printf(
				"psram stress FAIL %s @%u expect=0x%08x "
				"got=0x%08x xor=0x%08x\r\n",
				tag, (unsigned)i, (unsigned)expect,
				(unsigned)got, (unsigned)(expect ^ got));
			return -1;
		}
	}
	return 0;
}

static int check_guard(volatile uint32_t *g, uint32_t nwords)
{
	uint32_t i;
	uint32_t expect;
	uint32_t got;

	for (i = 0u; i < nwords; i++) {
		expect = guard_pat(i);
		got = g[i];
		if (got != expect) {
			board_console_printf(
				"psram stress FAIL guard @%u expect=0x%08x "
				"got=0x%08x (FB overrun?)\r\n",
				(unsigned)i, (unsigned)expect, (unsigned)got);
			return -1;
		}
	}
	return 0;
}

/*
 * Dirty rows of the off-screen FB and write them back — same PSRAM port and
 * same clean_area path the LVGL flush uses, without tearing the live frame.
 */
static void hammer_fb_writeback(uint32_t pass)
{
	uint16_t *fb;
	uint32_t y;
	uint32_t x;
	uint32_t row_words;

	fb = display_fb(pass & 1u);
	if (!fb)
		return;

	row_words = FB_STRIDE / sizeof(uint32_t);
	for (y = 0u; y < ULMK_BOARD_DISPLAY_H; y += 17u) {
		volatile uint32_t *row =
			(volatile uint32_t *)(void *)(fb + y * ULMK_BOARD_DISPLAY_W);

		for (x = 0u; x < row_words; x++)
			row[x] = pattern(pass, y + x);
		board_dcache_clean_area(fb, FB_STRIDE, 0, (int32_t)y,
					(int32_t)ULMK_BOARD_DISPLAY_W, 17,
					ULMK_BOARD_DISPLAY_BPP);
	}
}

int psram_stress_under_scanout(void)
{
	volatile uint32_t *stress;
	volatile uint32_t *guard;
	uint32_t nwords;
	uint32_t gwords;
	uint32_t pass;
	uint32_t i;
	uint32_t t0;
	uint32_t t1;

	if (STRESS_BYTES < 4096u) {
		board_console_puts("psram stress: no free region\r\n");
		return -1;
	}

	stress = (volatile uint32_t *)(uintptr_t)STRESS_BASE;
	guard = (volatile uint32_t *)(uintptr_t)GUARD_BASE;
	nwords = STRESS_BYTES / sizeof(uint32_t);
	gwords = GUARD_BYTES / sizeof(uint32_t);

	board_console_printf(
		"psram stress start @%08x %uKiB guard@%08x %uKiB passes=%u "
		"(+FB writeback hammer)\r\n",
		(unsigned)STRESS_BASE, (unsigned)(STRESS_BYTES / 1024u),
		(unsigned)GUARD_BASE, (unsigned)(GUARD_BYTES / 1024u),
		(unsigned)PASSES);

	/*
	 * Re-pick the delayline while DW_GDMA is already scanning: the boot
	 * refine only sees idle AXI and can land on a marginal point.
	 */
	if (board_psram_timing_refine_axi((void *)(uintptr_t)STRESS_BASE,
					  4096u) != 0) {
		board_console_puts("psram stress: axi refine under scanout "
				   "FAIL\r\n");
		return -1;
	}

	for (i = 0u; i < gwords; i++)
		guard[i] = guard_pat(i);
	board_dcache_clean((const void *)(uintptr_t)GUARD_BASE, GUARD_BYTES);

	t0 = board_timer_now_ms();

	for (pass = 0u; pass < PASSES; pass++) {
		for (i = 0u; i < nwords; i++)
			stress[i] = pattern(pass, i);
		__asm__ volatile("fence rw, rw" ::: "memory");

		hammer_fb_writeback(pass);

		if (check_words(stress, nwords, pass, "cached") != 0)
			return -1;

		board_dcache_clean((const void *)(uintptr_t)STRESS_BASE,
				   STRESS_BYTES);
		board_dcache_invalidate((const void *)(uintptr_t)STRESS_BASE,
					STRESS_BYTES);
		if (check_words(stress, nwords, pass, "refetch") != 0)
			return -1;

		/* Hold the pattern while GDMA keeps scanning. */
		(void)ulmk_sleep_ms(33u);
		hammer_fb_writeback(pass + 1u);
		board_dcache_invalidate((const void *)(uintptr_t)STRESS_BASE,
					STRESS_BYTES);
		if (check_words(stress, nwords, pass, "after-dma") != 0)
			return -1;

		board_console_printf("psram stress pass %u ok\r\n",
				     (unsigned)(pass + 1u));
	}

	board_dcache_invalidate((const void *)(uintptr_t)GUARD_BASE,
				GUARD_BYTES);
	if (check_guard(guard, gwords) != 0)
		return -1;

	t1 = board_timer_now_ms();
	board_console_printf("psram stress PASS in %ums\r\n",
			     (unsigned)(t1 - t0));
	return 0;
}
