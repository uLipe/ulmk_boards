/* SPDX-License-Identifier: MIT */
/*
 * Early board bring-up — before .data/.bss. Keep BSS-free.
 * PSRAM lives in board_services_init() after BSS clear.
 *
 * Bootloader leaves LP_WDT (RWDT) armed; IDF apps disable it in
 * startup. Without that, CHIP_LP_WDT_RESET hits after a few seconds.
 */
#include <stdint.h>
#include "board_console.h"

void board_pmp_allow_u_console(void);

#define LP_WDT_BASE		0x50116000u
#define LP_WDT_CONFIG0		(LP_WDT_BASE + 0x00u)
#define LP_WDT_WPROTECT		(LP_WDT_BASE + 0x18u)
#define LP_WDT_SWD_CONFIG	(LP_WDT_BASE + 0x1cu)
#define LP_WDT_SWD_WPROTECT	(LP_WDT_BASE + 0x20u)
#define LP_WDT_WKEY		0x50D83AA1u
#define LP_WDT_EN		(1u << 31)
#define LP_WDT_FLASHBOOT_EN	(1u << 12)
#define LP_WDT_SWD_AUTO_FEED	(1u << 18)
#define LP_WDT_SWD_DISABLE	(1u << 30)

#define TIMG0_BASE		0x500C2000u
#define TIMG_WDTCONFIG0		(TIMG0_BASE + 0x48u)
#define TIMG_WDTWPROTECT	(TIMG0_BASE + 0x64u)
#define TIMG_WDT_WKEY		0x50D83AA1u
#define TIMG_WDT_EN		(1u << 31)
#define TIMG_WDT_FLASHBOOT_EN	(1u << 14)

static inline void wr32(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd32(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void board_wdt_disable(void)
{
	uint32_t v;

	/* Super WDT: keep auto-feed (bootloader already sets this). */
	wr32(LP_WDT_SWD_WPROTECT, LP_WDT_WKEY);
	v = rd32(LP_WDT_SWD_CONFIG);
	wr32(LP_WDT_SWD_CONFIG, v | LP_WDT_SWD_AUTO_FEED);
	wr32(LP_WDT_SWD_WPROTECT, 0u);

	/*
	 * LP_WDT / RWDT: flashboot mode can fire even with WDT_EN=0.
	 * Bootloader arms stage0 for itself — disarm both.
	 */
	wr32(LP_WDT_WPROTECT, LP_WDT_WKEY);
	v = rd32(LP_WDT_CONFIG0);
	v &= ~(LP_WDT_EN | LP_WDT_FLASHBOOT_EN);
	wr32(LP_WDT_CONFIG0, v);
	wr32(LP_WDT_WPROTECT, 0u);

	/* TIMG0 MWDT flashboot (independent of enable bit). */
	wr32(TIMG_WDTWPROTECT, TIMG_WDT_WKEY);
	v = rd32(TIMG_WDTCONFIG0);
	v &= ~(TIMG_WDT_EN | TIMG_WDT_FLASHBOOT_EN);
	wr32(TIMG_WDTCONFIG0, v);
	wr32(TIMG_WDTWPROTECT, 0u);
}

void ulmk_board_init(void)
{
	board_wdt_disable();
	board_pmp_allow_u_console();
	board_console_early_puts("ulmk: board_init\n");
}
