/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_console.h — userspace console API (ASCLIN0 via IPC server).
 */

#ifndef BOARD_CONSOLE_H
#define BOARD_CONSOLE_H

#include <ulmk/microkernel.h>

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);

#endif /* BOARD_CONSOLE_H */
