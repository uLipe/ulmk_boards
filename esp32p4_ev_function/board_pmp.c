/* SPDX-License-Identifier: MIT */
/*
 * Board PMP extras — LP console + PSRAM window as stable grants.
 * Temp MSPI config uses ulmk_arch_pmp_map_temp / unmap_temp.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_arch.h>
#include "board_config.h"

#define LP_WIN_BASE		0x50100000u
#define LP_WIN_SIZE		0x00040000u /* 256 KiB */
#define PSRAM_WIN_BASE		ULMK_BOARD_PSRAM_BASE
#define PSRAM_WIN_SIZE		0x00800000u /* 8 MiB — FBs + LVGL heap */
/* Non-cache alias (SOC_NON_CACHEABLE_OFFSET) — CPU paint without writeback. */
#define PSRAM_NC_BASE		(ULMK_BOARD_PSRAM_BASE + 0x40000000u)
#define PSRAM_NC_SIZE		PSRAM_WIN_SIZE
#define HP_PERI_BASE		ULMK_BOARD_PERIPH_BASE
#define HP_PERI_SIZE		ULMK_BOARD_PERIPH_SIZE
/*
 * Non-cache alias of internal L2MEM: DMA descriptors (DW_GDMA LLI) are
 * written through it so no cache writeback is needed to publish them.
 */
#define SRAM_NC_BASE		0x8FF00000u
#define SRAM_NC_SIZE		0x00100000u /* 1 MiB */

/* Stable high slots below TEMP0/TEMP1 (14/15). */
#define PMP_SLOT_SRAM_NC	9u
#define PMP_SLOT_LP		10u
#define PMP_SLOT_PSRAM		11u
#define PMP_SLOT_PSRAM_NC	12u
#define PMP_SLOT_PERI		13u

void ulmk_board_pmp_extra(void)
{
	if (ULMK_ARCH_PMP_NUM == 0u)
		return;

	(void)ulmk_arch_pmp_set_napot(PMP_SLOT_SRAM_NC, SRAM_NC_BASE,
				      SRAM_NC_SIZE,
				      ULMK_PERM_READ | ULMK_PERM_WRITE);
	(void)ulmk_arch_pmp_set_napot(PMP_SLOT_LP, LP_WIN_BASE, LP_WIN_SIZE,
				      ULMK_PERM_READ | ULMK_PERM_WRITE |
				      ULMK_PERM_EXEC);
	(void)ulmk_arch_pmp_set_napot(PMP_SLOT_PSRAM, PSRAM_WIN_BASE,
				      PSRAM_WIN_SIZE,
				      ULMK_PERM_READ | ULMK_PERM_WRITE);
	(void)ulmk_arch_pmp_set_napot(PMP_SLOT_PSRAM_NC, PSRAM_NC_BASE,
				      PSRAM_NC_SIZE,
				      ULMK_PERM_READ | ULMK_PERM_WRITE);
	/* Full HP peri — TWAI/GPSPI/GPIO live here; boot map may be partial. */
	(void)ulmk_arch_pmp_set_napot(PMP_SLOT_PERI, HP_PERI_BASE, HP_PERI_SIZE,
				      ULMK_PERM_READ | ULMK_PERM_WRITE);
}

/*
 * Early board_init (M-mode, before arch mpu_init): open LP so ROM console
 * works until kernel layout takes over.  With PMP_NUM>0, mpu_init replaces
 * this via ulmk_board_pmp_extra().
 */
void board_pmp_allow_u_console(void)
{
	int slot;

	if (ULMK_ARCH_PMP_NUM == 0u) {
		/* Legacy path kept for reference builds — prefer PMP_NUM=16. */
		slot = ulmk_arch_pmp_map_temp(LP_WIN_BASE, LP_WIN_SIZE,
					      ULMK_PERM_READ | ULMK_PERM_WRITE |
					      ULMK_PERM_EXEC);
		(void)slot;
		return;
	}

	/*
	 * Before mpu_init the arch helpers still program CSRs.  Map LP
	 * temporarily so the early console works; mpu_init will clear and
	 * reinstall via ulmk_board_pmp_extra().
	 */
	(void)ulmk_arch_pmp_map_temp(LP_WIN_BASE, LP_WIN_SIZE,
				     ULMK_PERM_READ | ULMK_PERM_WRITE |
				     ULMK_PERM_EXEC);
}
