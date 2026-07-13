/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "adc_internal.h"
#include "board_config.h"

static void channel_of(uint8_t n, uint8_t *group, uint8_t *channel)
{
	/* Lite Kit: only channel 0 (pot) for now. */
	(void)n;
	*group = ULMK_BOARD_ADC0_GROUP;
	*channel = ULMK_BOARD_ADC0_CHANNEL;
}

int adc_config(uint8_t n)
{
	ulmk_msg_t msg;
	uint8_t group;
	uint8_t channel;
	int rc;

	if (n >= ADC_MAX || g_adc_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	channel_of(n, &group, &channel);
	msg.label    = ADC_MSG_CONFIG;
	msg.words[0] = group;
	msg.words[1] = channel;
	rc = ulmk_ep_call(g_adc_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int adc_read(uint8_t n, uint16_t *out)
{
	ulmk_msg_t msg;
	uint8_t group;
	uint8_t channel;
	int rc;

	if (n >= ADC_MAX || g_adc_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	channel_of(n, &group, &channel);
	msg.label    = ADC_MSG_READ;
	msg.words[0] = group;
	msg.words[1] = channel;
	rc = ulmk_ep_call(g_adc_ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	if (out)
		*out = (uint16_t)msg.words[1];
	return ULMK_OK;
}
