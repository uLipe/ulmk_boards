/* SPDX-License-Identifier: MIT */
/*
 * Kernel printk sink — UARTA data register.
 * Linked into libulmk_kernel.a (same pattern as qemu_printk_hook.c).
 */

#include <stdint.h>
#include <board_config.h>

#define MMIO32(a)	(*(volatile uint32_t *)(uintptr_t)(a))
#define MMIO8(a)	(*(volatile uint8_t *)(uintptr_t)(a))

#define UARTA_BASE	ULMK_BOARD_UARTA_BASE
#define UART_O_DR	0x00u
#define UART_O_FR	0x18u
#define UART_FR_TXFF	0x20u
#define C29_DSTS_INTE	(1u << 0)

void ulmk_board_uart_putc(char c)
{
	uint32_t dsts;

	/*
	 * Mask INT briefly so tick ISR cannot drop characters on the
	 * polled TX path.  Restore prior INTE — do not force-enable.
	 */
	__asm__ volatile(
		"ST.32	%0, DSTS\n\t"
		"DISINT"
		: "=m"(dsts) : : "memory");
	while (MMIO32(UARTA_BASE + UART_O_FR) & UART_FR_TXFF)
		;
	MMIO8(UARTA_BASE + UART_O_DR) = (uint8_t)c;
	if (dsts & C29_DSTS_INTE)
		__asm__ volatile("ENINT" ::: "memory");
}

void ulmk_printk_char_out(char c)
{
	if (c == '\n')
		ulmk_board_uart_putc('\r');
	ulmk_board_uart_putc(c);
}
