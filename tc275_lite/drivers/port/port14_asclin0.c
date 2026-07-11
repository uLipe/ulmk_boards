/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/drivers/port/port14_asclin0.c — pin mux for ASCLIN0 on P14.0 / P14.1.
 *
 * Lite Kit default USB–UART bridge:
 *   P14.0 — ASCLIN0 TX
 *   P14.1 — ASCLIN0 RX
 */

#include <stdint.h>

#define PORT14_BASE	0xF003B400u
#define P14_IOCR0	(PORT14_BASE + 0x10u)

void port14_asclin0_init(void)
{
	volatile uint32_t *iocr0 = (volatile uint32_t *)(uintptr_t)P14_IOCR0;
	uint32_t v;

	v = *iocr0;
	/* P14.0: ASCLIN0 TX — push-pull alternate output 2 (iLLD 0x90). */
	v &= ~(0xFFu << 0);
	v |= 0x90u << 0;
	/* P14.1: ASCLIN0 RX — input with pull-up (iLLD 0x10). */
	v &= ~(0xFFu << 8);
	v |= 0x10u << 8;
	*iocr0 = v;
}
