/* SPDX-License-Identifier: MIT */
#ifndef ADC_INTERNAL_H
#define ADC_INTERNAL_H
#include <ulmk/microkernel.h>
#include <adc.h>
#define ADC_MSG_READ	1u
#define ADC_MSG_SCAN	2u
extern ulmk_ep_t g_adc_ep;

int adc_hw_init(void);
/* Fills ADC_CH_MAX entries from a single conversion frame. */
int adc_hw_scan(uint16_t *out);
int adc_hw_read(uint8_t ch, uint16_t *out);
#endif
