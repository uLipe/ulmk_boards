/* SPDX-License-Identifier: MIT */
#ifndef BOARD_TIMER_H
#define BOARD_TIMER_H

#include <ulmk/microkernel.h>
#include <stdint.h>

ulmk_tid_t board_timer_start(const ulmk_boot_info_t *info);
void board_timer_sleep_us(uint32_t us);
uint32_t board_timer_now_ticks(void);
uint32_t board_timer_now_ms(void);
uint32_t board_timer_ticks_to_ns(uint32_t dt);

#endif /* BOARD_TIMER_H */
