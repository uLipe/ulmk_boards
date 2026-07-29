/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CONSOLE_H
#define BOARD_CONSOLE_H

#include <ulmk/microkernel.h>

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info);
void       board_console_putc(char c);
void       board_console_puts(const char *s);
void       board_console_printf(const char *fmt, ...);

/*
 * For ulmk_board_init(), which runs before .data copy and .bss zero: reads no
 * static state and issues no syscall, unlike the calls above.
 */
void       board_console_early_puts(const char *s);

#endif /* BOARD_CONSOLE_H */
