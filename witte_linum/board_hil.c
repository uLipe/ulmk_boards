/* SPDX-License-Identifier: MIT */
/*
 * witte_linum/board_hil.c — userspace milestone marker for HIL / J-Link.
 */
#include <stdint.h>

volatile uint32_t g_ulmk_board_hil_scratch
	__attribute__((section(".user_bss")));

/* ARM has no CSA pool — satisfy silicon_stress linker refs. */
uint8_t _ulmk_csa_pool_start;
uint8_t _ulmk_csa_pool_end;

void ulmk_board_hil_mark(uint32_t n)
{
	g_ulmk_board_hil_scratch = n;
}
