/* SPDX-License-Identifier: MIT */
#ifndef BOARD_ADC_H
#define BOARD_ADC_H

#include <ulmk/microkernel.h>
#include <stdint.h>

ulmk_tid_t board_adc_start(const ulmk_boot_info_t *info);
int board_adc_config(uint32_t channel);
int board_adc_read(uint32_t channel, uint32_t *raw);
int board_adc_subscribe(ulmk_notif_t n, uint32_t bit);

#endif /* BOARD_ADC_H */
