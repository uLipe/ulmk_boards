/* SPDX-License-Identifier: MIT */
/*
 * Kernel printk → UART0 FIFO.
 *
 * Deliberately avoids the ROM console: those routines fault when entered
 * from a user-privileged thread, and printk is reachable from M-mode boot
 * code, trap dumps and driver threads alike.  Neither interrupts nor
 * notifications exist on this path, so TX space is polled under a bounded
 * spin; userspace console traffic goes through the UART driver instead.
 */
#include <stdint.h>
#include "board_config.h"

#define UART_FIFO		(ULMK_BOARD_UART0_BASE + 0x00u)
#define UART_STATUS		(ULMK_BOARD_UART0_BASE + 0x1cu)
#define TXFIFO_CNT_SHIFT	16
#define TXFIFO_CNT_MASK		0xFFu
#define TXFIFO_DEPTH		128u
/* Bounded so a wedged UART cannot hang boot or a trap dump. */
#define TX_SPIN_LIMIT		200000u

static void putc_raw(uint8_t c)
{
	uint32_t spins = 0u;

	while (((*(volatile uint32_t *)UART_STATUS >> TXFIFO_CNT_SHIFT) &
		TXFIFO_CNT_MASK) >= TXFIFO_DEPTH) {
		if (++spins >= TX_SPIN_LIMIT)
			return;
	}
	*(volatile uint32_t *)UART_FIFO = c;
}

void ulmk_printk_char_out(char c)
{
	if (c == '\n')
		putc_raw((uint8_t)'\r');
	putc_raw((uint8_t)c);
}
