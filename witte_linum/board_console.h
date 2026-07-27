/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CONSOLE_H
#define BOARD_CONSOLE_H

#include <ulmk/microkernel.h>

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void board_console_printf(const char *fmt, ...);
int board_console_getc(char *out);

#endif /* BOARD_CONSOLE_H */
