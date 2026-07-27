/* SPDX-License-Identifier: MIT */
/*
 * CMSIS SystemCoreClock — defined here (not ST system_stm32h7xx.c) so we keep
 * our own startup.S.  Safe after .data relocation; board_init must not touch it.
 */
#include <stdint.h>
#include "board_config.h"

uint32_t SystemCoreClock = ULMK_BOARD_FCPU_HZ;

void SystemCoreClockUpdate(void)
{
	SystemCoreClock = ULMK_BOARD_FCPU_HZ;
}
