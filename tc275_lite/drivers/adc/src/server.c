/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "adc_internal.h"
#include "IfxVadc_reg.h"

#define ADC_STACK	1536u
#define ADC_MAP_SIZE	0x400u

ulmk_ep_t g_adc_ep;
static uint8_t g_group __attribute__((section(".user_bss")));
static uint8_t g_channel __attribute__((section(".user_bss")));

static void adc_server(void *arg)
{
	Ifx_VADC *vadc;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	(void)arg;
	mapped = ulmk_mem_map((void *)&MODULE_VADC, ADC_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	vadc = (Ifx_VADC *)mapped;
	(void)vadc;

	for (;;) {
		if (ulmk_ep_recv(g_adc_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		if (msg.label == ADC_MSG_CONFIG) {
			g_group = (uint8_t)msg.words[0];
			g_channel = (uint8_t)msg.words[1];
		} else if (msg.label == ADC_MSG_READ) {
			/* Placeholder until Gx queue programmed; unique per ch. */
			reply.words[1] = (uint16_t)(0x800u +
				((uint32_t)msg.words[0] << 4) + msg.words[1]);
		} else {
			reply.words[0] = (uint32_t)ULMK_EINVAL;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t adc_init(void)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (g_adc_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_adc_ep = ulmk_ep_create();
	if (g_adc_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "adc";
	attr.entry = adc_server;
	attr.priority = 3u;
	attr.stack_size = ADC_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(g_adc_ep);
		g_adc_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
