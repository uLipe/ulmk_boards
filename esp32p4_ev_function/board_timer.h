/* SPDX-License-Identifier: MIT */
#ifndef BOARD_TIMER_H
#define BOARD_TIMER_H

#include <stdint.h>
#include <ulmk/microkernel.h>

ulmk_tid_t board_timer_start(const ulmk_boot_info_t *info);
void       board_timer_sleep_us(uint32_t us);

/* Monotonic milliseconds from SYSTIMER unit0 (16 MHz). */
uint32_t   board_timer_now_ms(void);

#endif /* BOARD_TIMER_H */
