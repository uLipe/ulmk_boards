/* SPDX-License-Identifier: MIT */
/*
 * adc server — VADC queued conversion, IRQ + notif (no RES.VF busy-wait).
 *
 * Arm queue → TREV → ulmk_notif_wait(G0 SR0) → read RESULT / ack.
 * AN0 on the Lite Kit is group 0 / channel 0 (VAREF = 3.3 V).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "adc_internal.h"
#include "board_config.h"
#include "IfxVadc_reg.h"
#include "Vadc/Std/IfxVadc.h"

#define ADC_STACK		1536u
#define ADC_MAP_SIZE		0x1000u
#define ADC_MAX_GROUPS		8u
#define ADC_NOTIF_BIT		0u
#define ADC_SRPN		ULMK_BOARD_IRQ_VADC_G0
#define ADC_SRC			ULMK_BOARD_SRC_VADC_G0_SR0

ulmk_ep_t g_adc_ep;
static ulmk_notif_t g_irq_notif __attribute__((section(".user_bss")));
static uint8_t g_group __attribute__((section(".user_bss")));
static uint8_t g_channel __attribute__((section(".user_bss")));
static uint8_t g_group_ready[ADC_MAX_GROUPS]
	__attribute__((section(".user_bss")));

static void group_hw_init(Ifx_VADC_G *g)
{
	g->ARBCFG.B.ANONC = IfxVadc_AnalogConverterMode_normalOperation;
	g->ARBPR.B.PRIO0  = 1u;
	g->ARBPR.B.CSM0   = 0u;
	g->ARBPR.B.ASEN0  = 1u;

	g->ICLASS[0].B.CMS  = IfxVadc_ChannelResolution_12bit;
	g->ICLASS[0].B.STCS = 5u;

	IfxVadc_setQueueSlotGatingConfig(
		g, IfxVadc_GatingSource_0, IfxVadc_GatingMode_always);
	IfxVadc_disableQueueSlotExternalTrigger(g);
}

static void channel_hw_init(Ifx_VADC_G *g, uint8_t ch)
{
	IfxVadc_ChannelId cid = (IfxVadc_ChannelId)ch;
	IfxVadc_ChannelResult rreg = (IfxVadc_ChannelResult)ch;

	IfxVadc_setGroupPriorityChannel(g, cid);
	IfxVadc_setChannelInputClass(g, cid, IfxVadc_InputClasses_group0);
	IfxVadc_storeGroupResult(g, cid, rreg);
	IfxVadc_setResultNodeEventPointer0(g, IfxVadc_SrcNr_group0, rreg);
	IfxVadc_enableServiceRequest(g, rreg);
}

static int convert_once(Ifx_VADC_G *g, uint8_t ch, uint16_t *out)
{
	Ifx_VADC_RES res;
	uint32_t bits;
	int ret;

	/* Drop stale result / SRC before arming. */
	g->REFCLR.U = (1u << ch);
	ulmk_irq_ack(ADC_SRPN);

	IfxVadc_clearQueue(g, TRUE);
	IfxVadc_addToQueue(g, (IfxVadc_ChannelId)ch, 0u);
	IfxVadc_startQueue(g);

	bits = 0u;
	ret = ulmk_notif_wait(g_irq_notif, 1u << ADC_NOTIF_BIT, &bits);
	if (ret != ULMK_OK)
		return ret;

	res = IfxVadc_getResult(g, ch);
	g->REFCLR.U = (1u << ch);
	ulmk_irq_ack(ADC_SRPN);

	if (!res.B.VF)
		return ULMK_ETIMEOUT;
	*out = (uint16_t)res.B.RESULT;
	return ULMK_OK;
}

static void adc_server(void *arg)
{
	Ifx_VADC *vadc;
	Ifx_VADC_G *g;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t group;
	uint8_t channel;
	uint16_t raw;
	int rc;

	(void)arg;
	mapped = ulmk_mem_map((void *)&MODULE_VADC, ADC_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	vadc = (Ifx_VADC *)mapped;

	for (;;) {
		if (ulmk_ep_recv(g_adc_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;

		if (msg.label == ADC_MSG_CONFIG) {
			group = (uint8_t)msg.words[0];
			channel = (uint8_t)msg.words[1];
			if (group >= ADC_MAX_GROUPS || channel > 15u) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
			} else {
				g = &vadc->G[group];
				if (!g_group_ready[group]) {
					group_hw_init(g);
					g_group_ready[group] = 1u;
				}
				channel_hw_init(g, channel);
				g_group = group;
				g_channel = channel;
			}
		} else if (msg.label == ADC_MSG_READ) {
			group = (uint8_t)msg.words[0];
			channel = (uint8_t)msg.words[1];
			if (group >= ADC_MAX_GROUPS || channel > 15u) {
				reply.words[0] = (uint32_t)ULMK_EINVAL;
			} else {
				g = &vadc->G[group];
				if (!g_group_ready[group]) {
					group_hw_init(g);
					g_group_ready[group] = 1u;
					channel_hw_init(g, channel);
				}
				rc = convert_once(g, channel, &raw);
				reply.words[0] = (uint32_t)rc;
				reply.words[1] = raw;
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
	int ret;

	if (n != 0u)
		return ULMK_TID_INVALID;
	if (g_adc_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_adc_ep = ulmk_ep_create();
	if (g_adc_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_irq_notif = ulmk_notif_create();
	if (g_irq_notif == ULMK_NOTIF_INVALID) {
		ulmk_ep_destroy(g_adc_ep);
		g_adc_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}

	ret = ulmk_irq_bind_hw(ADC_SRPN, g_irq_notif, ADC_NOTIF_BIT,
			       (uintptr_t)ADC_SRC);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(ADC_SRPN);
	if (ret != ULMK_OK)
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
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
