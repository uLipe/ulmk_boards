/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "adc_internal.h"

int adc_read(uint8_t ch, uint16_t *value)
{
	ulmk_msg_t msg;
	int rc;
	if (!value)
		return ULMK_EINVAL;
	msg.label = ADC_MSG_READ;
	msg.words[0] = ch;
	rc = ulmk_ep_call(g_adc_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	*value = (uint16_t)msg.words[1];
	return ULMK_OK;
}

int adc_scan_all(uint16_t *values)
{
	ulmk_msg_t msg;
	int rc;
	uint32_t i;
	if (!values)
		return ULMK_EINVAL;
	msg.label = ADC_MSG_SCAN;
	rc = ulmk_ep_call(g_adc_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	for (i = 0u; i < ADC_CH_MAX; i++)
		values[i] = (i < 4u) ? (uint16_t)msg.words[1 + i] : 0u;
	return ULMK_OK;
}
