/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "adc_internal.h"

static int adc_call(ulmk_msg_t *msg)
{
	int ret;

	if (g_adc_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_adc_ep, msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

int adc_config(uint8_t n)
{
	ulmk_msg_t msg = {0};

	if (n >= ADC_CH_MAX)
		return ULMK_EINVAL;
	msg.label = ADC_MSG_CONFIG;
	msg.words[0] = n;
	return adc_call(&msg);
}

int adc_read(uint8_t n, uint16_t *out)
{
	ulmk_msg_t msg = {0};
	int ret;

	if (!out || n >= ADC_CH_MAX)
		return ULMK_EINVAL;
	msg.label = ADC_MSG_READ;
	msg.words[0] = n;
	ret = adc_call(&msg);
	if (ret == ULMK_OK)
		*out = (uint16_t)msg.words[1];
	return ret;
}

int adc_scan_all(uint16_t values[ADC_CH_MAX])
{
	uint8_t n;
	int ret;

	if (!values)
		return ULMK_EINVAL;
	for (n = 0u; n < ADC_CH_MAX; n++) {
		ret = adc_read(n, &values[n]);
		if (ret != ULMK_OK)
			return ret;
	}
	return ULMK_OK;
}
