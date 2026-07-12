/* SPDX-License-Identifier: MIT */
#ifndef ADC_INTERNAL_H
#define ADC_INTERNAL_H

#include <ulmk/microkernel.h>
#include <adc.h>

#define ADC_MSG_CONFIG	1u
#define ADC_MSG_READ	2u

extern ulmk_ep_t g_adc_ep;

#endif /* ADC_INTERNAL_H */
