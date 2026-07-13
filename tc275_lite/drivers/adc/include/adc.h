/* SPDX-License-Identifier: MIT */
#ifndef ADC_H
#define ADC_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include "board_config.h"

#define ADC_MAX	ULMK_BOARD_ADC_MAX

/*
 * @p n on adc_init is the VADC module instance (Lite: 0).
 * @p n on config/read is the logical channel (board_config group/channel).
 */
ulmk_tid_t adc_init(uint8_t n);
int adc_config(uint8_t n);
int adc_read(uint8_t n, uint16_t *out);

#endif /* ADC_H */
