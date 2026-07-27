/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_ll.h"
#include "adc_internal.h"
#include "pinmux_internal.h"

#define ADC_STACK_SIZE		2048u
#define ADC_DMA_MAP_SIZE	0xC00u
#define ADC_DMA_CLEAR_FLAGS	0x3Du
#define ADC_DMA_TCIF0		(1u << 5)
#define ADC_DMA_CR_TCIE		(1u << 4)
#define ADC_DMA_CR_PSIZE_16	(1u << 11)
#define ADC_DMA_CR_MSIZE_16	(1u << 13)
#define ADC_DMA_CR_EN		(1u << 0)
#define ADC_CFGR_DMAEN		(1u << 0)
#define ADC_DMAMUX_REQUEST_ADC1	9u

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
static ulmk_notif_t g_adc_notif __attribute__((section(".user_bss")));
static ADC_TypeDef *g_adc __attribute__((section(".user_bss")));
static DMA_TypeDef *g_dma __attribute__((section(".user_bss")));
static DMA_Stream_TypeDef *g_stream __attribute__((section(".user_bss")));
static DMAMUX_Channel_TypeDef *g_dmamux __attribute__((section(".user_bss")));
static uint16_t g_sample __attribute__((section(".user_bss")));

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
	ADC12_COMMON->CCR = 0u;
	g_adc->CFGR = ADC_CFGR_DMAEN;
	g_adc->CR |= ADC_CR_ADEN;
	if ((g_adc->ISR & ADC_ISR_ADRDY) == 0u)
		return ULMK_ETIMEOUT;
	return ULMK_OK;
}

static int adc_one_shot(uint8_t n, uint16_t *out)
{
	uint32_t bits;
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
	g_dma->LIFCR = ADC_DMA_CLEAR_FLAGS;
	g_stream->CR = 0u;
	g_stream->PAR = (uint32_t)(uintptr_t)&g_adc->DR;
	g_stream->M0AR = (uint32_t)(uintptr_t)&g_sample;
	g_stream->NDTR = 1u;
	g_stream->CR = ADC_DMA_CR_TCIE | ADC_DMA_CR_PSIZE_16 |
		       ADC_DMA_CR_MSIZE_16;
	g_dmamux->CCR = ADC_DMAMUX_REQUEST_ADC1;
	ulmk_irq_ack(ULMK_BOARD_IRQ_DMA_ADC);
	g_stream->CR |= ADC_DMA_CR_EN;
	g_adc->CR |= ADC_CR_ADSTART;
	bits = 0u;
	ret = ulmk_notif_wait(g_adc_notif, 1u << ADC_NOTIF_DMA, &bits);
	if (ret != ULMK_OK)
		return ret;
	if ((g_dma->LISR & ADC_DMA_TCIF0) == 0u)
		return ULMK_ETIMEOUT;
	g_dma->LIFCR = ADC_DMA_CLEAR_FLAGS;
	ulmk_irq_ack(ULMK_BOARD_IRQ_DMA_ADC);
	*out = g_sample;
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
	g_dma = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_DMA1_BASE,
			     ADC_DMA_MAP_SIZE, ULMK_PERM_READ | ULMK_PERM_WRITE,
			     ULMK_MMAP_PERIPH);
	if (!g_adc || !g_dma)
		return;
	g_stream = (DMA_Stream_TypeDef *)((uintptr_t)g_dma + 0x10u);
	g_dmamux = (DMAMUX_Channel_TypeDef *)((uintptr_t)g_dma + 0x800u);
	(void)adc_prepare();
	for (;;) {
		if (ulmk_ep_recv(g_adc_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		if (msg.words[0] < ADC_CH_MAX && msg.label == ADC_MSG_CONFIG)
			reply.words[0] = (uint32_t)adc_configure_pad(msg.words[0]);
		else if (msg.words[0] < ADC_CH_MAX && msg.label == ADC_MSG_READ) {
			reply.words[0] = (uint32_t)adc_one_shot(msg.words[0], &sample);
			reply.words[1] = sample;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t adc_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (n != 0u || g_adc_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_adc_ep = ulmk_ep_create();
	g_adc_notif = ulmk_notif_create();
	if (g_adc_ep == ULMK_EP_INVALID || g_adc_notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;
	if (ulmk_irq_bind_hw(ULMK_BOARD_IRQ_DMA_ADC, g_adc_notif, ADC_NOTIF_DMA,
			     ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_DMA1_STR0)) != ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_DMA_ADC) != ULMK_OK)
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
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
