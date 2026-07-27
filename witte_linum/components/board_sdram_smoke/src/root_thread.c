/* SPDX-License-Identifier: MIT */
/*
 * board_sdram_smoke — verify early FMC probe + ULMK_MMAP_SHARED R/W.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_console.h"
#include "board_services.h"
#include "board_sdram.h"
#include "board_config.h"

#define SDRAM_SMOKE_MAP_SIZE	(64u * 1024u)
#define SDRAM_MARK_ADDR		0x2407FF00u
#define SDRAM_MARK_OK		0x53444F4Bu

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	void *map;
	volatile uint32_t *mark = (volatile uint32_t *)SDRAM_MARK_ADDR;
	int ok;

	board_services_init(info);

	board_console_puts("\r\n");
	board_console_puts("ulmk: Witte-Linum SDRAM smoke\r\n");

	if (*mark != SDRAM_MARK_OK) {
		board_console_puts("SDRAM: FAIL (early probe)\r\n");
		ulmk_thread_exit();
	}

	map = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_SDRAM_BASE,
			   SDRAM_SMOKE_MAP_SIZE,
			   ULMK_PERM_READ | ULMK_PERM_WRITE,
			   ULMK_MMAP_SHARED);
	if (map == NULL) {
		board_console_puts("SDRAM: FAIL (mmap)\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("SDRAM: mmap ok\r\n");

	ok = board_sdram_ready();
	if (ok)
		board_console_puts("SDRAM: PASS\r\n");
	else
		board_console_puts("SDRAM: FAIL (probe)\r\n");

	for (;;)
		ulmk_thread_yield();
}
