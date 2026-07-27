/* SPDX-License-Identifier: MIT */

#include <stdint.h>
#include <ulmk/microkernel.h>

void board_console_putc(char c)
{
	extern void ulmk_board_uart_putc(char c);

	ulmk_board_uart_putc(c);
}

void board_console_puts(const char *s)
{
	if (!s)
		return;
	while (*s)
		board_console_putc(*s++);
}
