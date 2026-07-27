/* SPDX-License-Identifier: MIT */
/*
 * board_sdram.c — report early board_init probe; optional userspace R/W.
 */
#include <stdint.h>
#include "board_sdram.h"
#include "board_config.h"

#define SDRAM_MARK_ADDR	0x2407FF00u
#define SDRAM_MARK_OK	0x53444F4Bu	/* "SDOK" */
#define SDRAM_MAGIC_A	0xA5A55A5Au
#define SDRAM_MAGIC_B	0x5A5AA5A5u

int board_sdram_probe(void)
{
	volatile uint32_t *mark = (volatile uint32_t *)SDRAM_MARK_ADDR;
	volatile uint32_t *p;
	uint32_t v0;
	uint32_t v1;

	if (*mark != SDRAM_MARK_OK)
		return -1;

	p = (volatile uint32_t *)(uintptr_t)ULMK_BOARD_SDRAM_BASE;
	p[0] = SDRAM_MAGIC_A;
	p[1] = SDRAM_MAGIC_B;
	__asm__ volatile("dsb" ::: "memory");
	v0 = p[0];
	v1 = p[1];
	if (v0 != SDRAM_MAGIC_A || v1 != SDRAM_MAGIC_B)
		return -1;
	return 0;
}

int board_sdram_ready(void)
{
	volatile uint32_t *mark = (volatile uint32_t *)SDRAM_MARK_ADDR;

	if (*mark != SDRAM_MARK_OK)
		return 0;
	return board_sdram_probe() == 0 ? 1 : 0;
}
