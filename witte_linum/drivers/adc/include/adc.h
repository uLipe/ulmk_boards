/* SPDX-License-Identifier: MIT */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"

#define ADC_MAX	ULMK_BOARD_ADC_MAX
#define ADC_CH_MAX	ULMK_BOARD_ADC_CH_MAX

ulmk_tid_t adc_init(uint8_t n);
int adc_config(uint8_t n);
int adc_read(uint8_t n, uint16_t *out);
int adc_scan_all(uint16_t values[ADC_CH_MAX]);

#endif /* ADC_H */
