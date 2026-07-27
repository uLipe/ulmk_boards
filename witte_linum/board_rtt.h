/* SPDX-License-Identifier: MIT */
#ifndef BOARD_RTT_H
#define BOARD_RTT_H

#include <stdint.h>
#include <stddef.h>

void board_rtt_init(void);
void board_rtt_putc(char c);
void board_rtt_write(const char *buf, uint32_t len);
int board_rtt_getc(char *out);

#endif /* BOARD_RTT_H */
