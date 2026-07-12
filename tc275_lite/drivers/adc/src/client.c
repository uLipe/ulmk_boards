/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "adc_internal.h"

int adc_config(const adc_channel_t *ch)
{
	ulmk_msg_t msg;
	int rc;

	if (!ch || g_adc_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	msg.label    = ADC_MSG_CONFIG;
	msg.words[0] = ch->group;
	msg.words[1] = ch->channel;
	rc = ulmk_ep_call(g_adc_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int adc_read(const adc_channel_t *ch, uint16_t *out)
{
	ulmk_msg_t msg;
	int rc;

	if (!ch || g_adc_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	msg.label    = ADC_MSG_READ;
	msg.words[0] = ch->group;
	msg.words[1] = ch->channel;
	rc = ulmk_ep_call(g_adc_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	if (out)
		*out = (uint16_t)msg.words[1];
	return ULMK_OK;
}
