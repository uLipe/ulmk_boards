/* SPDX-License-Identifier: MIT */
/*
 * Board timer — wrapper over kernel sleep (ulmk_sleep_ms).
 */

#include <stdint.h>
#include <ulmk/microkernel.h>

void board_timer_sleep_us(uint32_t us)
{
	uint32_t ms;

	if (us == 0u)
		return;
	ms = (us + 999u) / 1000u;
	if (ms == 0u)
		ms = 1u;
	(void)ulmk_sleep_ms(ms);
}
