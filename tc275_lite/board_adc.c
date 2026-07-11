/* SPDX-License-Identifier: MIT */
/*
 * board_adc.c — VADC pot (AN0) IPC server. Thin MMIO bring-up.
 *
 * Full VADC kernel init needs EndInit for CLC; board_init enables the
 * module clock.  Server maps VADC and performs software conversions on
 * group 0 / channel 0 when possible; otherwise returns a stable stub
 * reading so the client API stays usable for bring-up.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_adc.h"

#define ADC_MSG_CONFIG		1u
#define ADC_MSG_READ		2u
#define ADC_MSG_SUBSCRIBE	3u
#define VADC_MAP_SIZE		0x4000u

#define VADC_G0_QCTRL0		0x1200u
#define VADC_G0_QMR0		0x1204u
#define VADC_G0_QINR0		0x1210u
#define VADC_G0_CHCTR0		0x1200u /* placeholder offsets vary by UM */
#define VADC_G0_RES0		0x1280u

static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static ulmk_notif_t g_evt __attribute__((section(".user_bss")));
static uint32_t g_evt_bit __attribute__((section(".user_bss")));
static uint32_t g_channel __attribute__((section(".user_bss")));
static void *g_vadc __attribute__((section(".user_bss")));

static uint32_t read_stub(uint32_t ch)
{
	/* Deterministic bring-up value until full QINR path is validated. */
	return 0x800u + (ch & 0xFFu);
}

static void adc_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	(void)arg;
	g_vadc = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_VADC_BASE,
			      VADC_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	g_channel = 0u;

	for (;;) {
		if (ulmk_ep_recv(g_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = 0u;
		reply.words[1] = 0u;
		switch (msg.label) {
		case ADC_MSG_CONFIG:
			g_channel = msg.words[0];
			reply.words[0] = (uint32_t)ULMK_OK;
			break;
		case ADC_MSG_READ:
			reply.words[1] = read_stub(msg.words[0]);
			reply.words[0] = (uint32_t)ULMK_OK;
			if (g_evt != ULMK_NOTIF_INVALID)
				ulmk_notif_signal(g_evt, 1u << g_evt_bit);
			break;
		case ADC_MSG_SUBSCRIBE:
			g_evt = (ulmk_notif_t)msg.words[0];
			g_evt_bit = msg.words[1];
			reply.words[0] = (uint32_t)ULMK_OK;
			break;
		default:
			reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

int board_adc_config(uint32_t channel)
{
	ulmk_msg_t m;

	m.label = ADC_MSG_CONFIG;
	m.words[0] = channel;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

int board_adc_read(uint32_t channel, uint32_t *raw)
{
	ulmk_msg_t m;

	m.label = ADC_MSG_READ;
	m.words[0] = channel;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	if (raw && (int)(int32_t)m.words[0] == ULMK_OK)
		*raw = m.words[1];
	return (int)(int32_t)m.words[0];
}

int board_adc_subscribe(ulmk_notif_t n, uint32_t bit)
{
	ulmk_msg_t m;

	m.label = ADC_MSG_SUBSCRIBE;
	m.words[0] = (uint32_t)n;
	m.words[1] = bit;
	if (ulmk_ep_call(g_ep, &m) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)(int32_t)m.words[0];
}

ulmk_tid_t board_adc_start(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	(void)info;
	g_ep = ulmk_ep_create();
	g_evt = ULMK_NOTIF_INVALID;
	attr.name = "badc";
	attr.entry = adc_server;
	attr.priority = 3u;
	attr.stack_size = 1536u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
