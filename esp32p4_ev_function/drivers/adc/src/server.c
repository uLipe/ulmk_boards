/* SPDX-License-Identifier: MIT */
/* ADC1 oneshot server — LP RTCADC MMIO only (no soft fake values). */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "adc_internal.h"
#include "board_config.h"

ulmk_ep_t g_adc_ep;
static uint8_t g_hw_ok;

static void adc_server(void *arg)
{
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;
	uint16_t all[ADC_CH_MAX];
	uint32_t i;
	uint16_t v;
	int rc;

	(void)arg;
	g_hw_ok = (adc_hw_init() == ULMK_OK) ? 1u : 0u;

	for (;;) {
		if (ulmk_ep_recv(g_adc_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		if (!g_hw_ok) {
			reply.words[0] = (uint32_t)ULMK_ENOTSUP;
		} else if (msg.label == ADC_MSG_READ) {
			rc = adc_hw_read((uint8_t)msg.words[0], &v);
			reply.words[0] = (uint32_t)rc;
			reply.words[1] = v;
		} else if (msg.label == ADC_MSG_SCAN) {
			rc = adc_hw_scan(all);
			reply.words[0] = (uint32_t)rc;
			if (rc == ULMK_OK) {
				for (i = 0u; i < ADC_CH_MAX && i < 4u; i++)
					reply.words[1 + i] = all[i];
			}
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t adc_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)n;
	g_adc_ep = ulmk_ep_create();
	if (g_adc_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "adc";
	attr.entry = adc_server;
	attr.priority = 1u;
	attr.stack_size = 1536u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
