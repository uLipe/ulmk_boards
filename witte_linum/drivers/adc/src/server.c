/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <dma.h>
#include "board_config.h"
#include "board_ll.h"
#include "adc_internal.h"
#include "pinmux_internal.h"

#define ADC_STACK_SIZE		2048u
/* H7 CFGR DMNGT=01 → DMA one-shot */
#define ADC_CFGR_DMNGT_DMA	(1u << 0)
#define ADC_DMA_WAIT_MS		100u
#define ADC_DR_OFF		0x40u

struct adc_pad {
	uint8_t port;
	uint8_t pin;
	uint8_t channel;
};

static const struct adc_pad g_pads[ADC_CH_MAX] = {
	{ 0u, 3u, ULMK_BOARD_ADC_CH0 }, { 0u, 4u, ULMK_BOARD_ADC_CH1 },
	{ 0u, 6u, ULMK_BOARD_ADC_CH2 }, { 1u, 0u, ULMK_BOARD_ADC_CH3 },
	{ 1u, 1u, ULMK_BOARD_ADC_CH4 }, { 8u, 0u, ULMK_BOARD_ADC_CH5 },
	{ 8u, 5u, ULMK_BOARD_ADC_CH6 }, { 7u, 2u, ULMK_BOARD_ADC_CH7 },
	{ 7u, 3u, ULMK_BOARD_ADC_CH8 }, { 8u, 6u, ULMK_BOARD_ADC_CH9 },
};

ulmk_ep_t g_adc_ep;
static ADC_TypeDef *g_adc __attribute__((section(".user_bss")));
static int g_adc_ready __attribute__((section(".user_bss")));

static int adc_configure_pad(uint8_t n)
{
	GPIO_TypeDef *gpio;
	uint32_t pin;

	if (n >= ADC_CH_MAX)
		return ULMK_EINVAL;
	gpio = pinmux_gpio(g_pads[n].port);
	if (!gpio)
		return ULMK_ENOMEM;
	pin = 1u << g_pads[n].pin;
	LL_GPIO_SetPinMode(gpio, pin, LL_GPIO_MODE_ANALOG);
	LL_GPIO_SetPinPull(gpio, pin, LL_GPIO_PULL_NO);
	return ULMK_OK;
}

static int adc_prepare(void)
{
	uint32_t tries;

	/* CKMODE = HCLK/2 (bits [17:16] = 10b) */
	ADC12_COMMON->CCR = (2u << 16);
	g_adc->CR &= ~ADC_CR_DEEPPWD;
	g_adc->CR |= ADC_CR_ADVREGEN;
	/* Regulator needs ~10 µs; sleep yields (no busy-wait). */
	(void)ulmk_sleep_ms(1u);

	g_adc->CFGR = ADC_CFGR_DMNGT_DMA;
	g_adc->CR |= ADC_CR_ADEN;
	for (tries = 0u; tries < 10u; tries++) {
		if (g_adc->ISR & ADC_ISR_ADRDY)
			return ULMK_OK;
		(void)ulmk_sleep_ms(1u);
	}
	return ULMK_ETIMEOUT;
}

static int adc_one_shot(uint8_t n, uint16_t *out)
{
	uint8_t raw[2];
	int ret;

	ret = adc_configure_pad(n);
	if (ret != ULMK_OK)
		return ret;
	g_adc->PCSEL |= 1u << g_pads[n].channel;
	if (g_pads[n].channel <= 9u)
		g_adc->SMPR1 |= 7u << (g_pads[n].channel * 3u);
	else
		g_adc->SMPR2 |= 7u << ((g_pads[n].channel - 10u) * 3u);
	g_adc->SQR1 = (uint32_t)g_pads[n].channel << 6;

	/* PAR must be the physical DR address (DMA ignores CPU MPU maps). */
	ret = dma_arm(ULMK_BOARD_DMA_SLOT_ADC,
		      (uintptr_t)ULMK_BOARD_ADC1_BASE + ADC_DR_OFF, NULL, 2u);
	if (ret != ULMK_OK)
		return ret;
	__asm__ volatile("dsb" ::: "memory");
	g_adc->CR |= ADC_CR_ADSTART;
	ret = dma_wait(ULMK_BOARD_DMA_SLOT_ADC, raw, 2u, ADC_DMA_WAIT_MS);
	if (ret != ULMK_OK)
		return ret;
	*out = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
	return ULMK_OK;
}

static void adc_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint16_t sample;

	(void)arg;
	g_adc = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_ADC1_BASE,
			     ULMK_BOARD_ADC_MAP_SIZE,
			     ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!g_adc)
		return;
	g_adc_ready = (adc_prepare() == ULMK_OK) ? 1 : 0;
	for (;;) {
		if (ulmk_ep_recv(g_adc_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		if (!g_adc_ready) {
			reply.words[0] = (uint32_t)ULMK_ETIMEOUT;
		} else if (msg.words[0] < ADC_CH_MAX &&
			   msg.label == ADC_MSG_CONFIG) {
			reply.words[0] = (uint32_t)adc_configure_pad(msg.words[0]);
		} else if (msg.words[0] < ADC_CH_MAX &&
			   msg.label == ADC_MSG_READ) {
			reply.words[0] = (uint32_t)adc_one_shot(msg.words[0],
								&sample);
			reply.words[1] = sample;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t adc_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	int ret;

	if (n != 0u || g_adc_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	if (dma_init() == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ret = dma_channel_open(ULMK_BOARD_DMA_SLOT_ADC, ULMK_BOARD_DMAMUX_ADC1,
			       DMA_FLAG_PSIZE_16 | DMA_FLAG_MSIZE_16);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	g_adc_ep = ulmk_ep_create();
	if (g_adc_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "adc";
	attr.entry = adc_server;
	attr.priority = 2u;
	attr.stack_size = ADC_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	return tid;
}
