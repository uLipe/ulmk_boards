/* SPDX-License-Identifier: MIT */
#ifndef ADC_H
#define ADC_H

#include <ulmk/microkernel.h>
#include <stdint.h>

typedef struct {
	uint8_t group;		/* VADC group */
	uint8_t channel;	/* channel within group */
} adc_channel_t;

ulmk_tid_t adc_init(void);
int adc_config(const adc_channel_t *ch);
int adc_read(const adc_channel_t *ch, uint16_t *out);

#endif /* ADC_H */
