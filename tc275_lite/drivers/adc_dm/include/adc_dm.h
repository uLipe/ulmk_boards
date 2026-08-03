/* SPDX-License-Identifier: MIT */
#ifndef ADC_DM_H
#define ADC_DM_H

#include <ulmk/microkernel.h>
#include <stdint.h>

/*
 * Device-manager adapter over the legacy VADC client (adc.h).
 * Path: /dev/adc0 (mod 0).
 */
ulmk_tid_t adc_dm_init(uint8_t mod);
ulmk_ep_t adc_dm_ep(void);

#endif /* ADC_DM_H */
