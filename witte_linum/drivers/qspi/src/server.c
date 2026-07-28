/* SPDX-License-Identifier: MIT */
/*
 * QUADSPI indirect mode — completion via TCF IRQ + notif (async).
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_ll.h"
#include "qspi_internal.h"

#define QSPI_STACK_SIZE		2048u

ulmk_ep_t g_qspi_ep;
static ulmk_notif_t g_qspi_notif __attribute__((section(".user_bss")));
static QUADSPI_TypeDef *g_qspi __attribute__((section(".user_bss")));
static uint8_t g_buf[16] __attribute__((section(".user_bss")));

static GPIO_TypeDef *qspi_map_gpio(uintptr_t phys)
{
	return (GPIO_TypeDef *)ulmk_mem_map((void *)phys, 0x400u,
					    ULMK_PERM_READ | ULMK_PERM_WRITE,
					    ULMK_MMAP_PERIPH);
}

static void qspi_pinmux_ll(void)
{
	GPIO_TypeDef *gpiof;
	GPIO_TypeDef *gpiog;
	/* CLK/NCS/IO0/IO1 as AF9; IO2/IO3 (WP#/HOLD#) forced high as GPIO. */
	static const uint32_t af_pins[] = {
		LL_GPIO_PIN_8, LL_GPIO_PIN_9, LL_GPIO_PIN_10,
	};
	uint32_t i;

	gpiof = qspi_map_gpio(ULMK_BOARD_GPIOA_BASE + 5u * ULMK_BOARD_GPIO_STRIDE);
	gpiog = qspi_map_gpio(ULMK_BOARD_GPIOA_BASE + 6u * ULMK_BOARD_GPIO_STRIDE);
	if (!gpiof || !gpiog)
		return;

	for (i = 0u; i < sizeof(af_pins) / sizeof(af_pins[0]); i++) {
		LL_GPIO_SetPinMode(gpiof, af_pins[i], LL_GPIO_MODE_ALTERNATE);
		LL_GPIO_SetPinSpeed(gpiof, af_pins[i],
				    LL_GPIO_SPEED_FREQ_VERY_HIGH);
		LL_GPIO_SetPinOutputType(gpiof, af_pins[i],
					 LL_GPIO_OUTPUT_PUSHPULL);
		LL_GPIO_SetPinPull(gpiof, af_pins[i], LL_GPIO_PULL_UP);
		if (af_pins[i] > LL_GPIO_PIN_7)
			LL_GPIO_SetAFPin_8_15(gpiof, af_pins[i], LL_GPIO_AF_9);
		else
			LL_GPIO_SetAFPin_0_7(gpiof, af_pins[i], LL_GPIO_AF_9);
	}
	/* PF7=IO2/WP#, PF6=IO3/HOLD# — must idle high in 1-1-1 SPI. */
	LL_GPIO_SetPinMode(gpiof, LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinMode(gpiof, LL_GPIO_PIN_7, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(gpiof, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinOutputType(gpiof, LL_GPIO_PIN_7, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetOutputPin(gpiof, LL_GPIO_PIN_6);
	LL_GPIO_SetOutputPin(gpiof, LL_GPIO_PIN_7);

	LL_GPIO_SetPinMode(gpiog, LL_GPIO_PIN_6, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetPinSpeed(gpiog, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_VERY_HIGH);
	LL_GPIO_SetPinOutputType(gpiog, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinPull(gpiog, LL_GPIO_PIN_6, LL_GPIO_PULL_UP);
	LL_GPIO_SetAFPin_0_7(gpiog, LL_GPIO_PIN_6, LL_GPIO_AF_9);
}

static void qspi_hw_init(void)
{
	g_qspi->CR = 0u;
	/* SSHIFT + slower edge until silicon timing is proven. */
	g_qspi->CR = (ULMK_BOARD_QSPI_PRESCALER << QUADSPI_CR_PRESCALER_Pos) |
		     QUADSPI_CR_SSHIFT | QUADSPI_CR_TCIE | QUADSPI_CR_EN;
	g_qspi->DCR = (23u << QUADSPI_DCR_FSIZE_Pos) |
		      (7u << QUADSPI_DCR_CSHT_Pos); /* CS high time */
}

static int wait_tc(void)
{
	uint32_t bits = 0u;
	int ret;

	ulmk_irq_ack(ULMK_BOARD_IRQ_QSPI);
	ret = ulmk_notif_wait(g_qspi_notif, 1u << QSPI_NOTIF_TC, &bits);
	ulmk_irq_ack(ULMK_BOARD_IRQ_QSPI);
	if (ret != ULMK_OK)
		return ret;
	if ((g_qspi->SR & QUADSPI_SR_TCF) == 0u)
		return ULMK_ETIMEOUT;
	/* Leave TCF set until after FIFO drain. */
	return ULMK_OK;
}

static void qspi_abort(void)
{
	uint32_t guard = 100000u;

	g_qspi->CR |= QUADSPI_CR_ABORT;
	while ((g_qspi->SR & QUADSPI_SR_BUSY) != 0u && guard-- > 0u)
		;
	g_qspi->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF | QUADSPI_FCR_CSMF;
}

static int qspi_drain(uint8_t *out, size_t len)
{
	size_t i;
	uint32_t guard;
	volatile uint8_t *dr8;

	dr8 = (volatile uint8_t *)(uintptr_t)&g_qspi->DR;
	for (i = 0u; i < len; i++) {
		guard = 1000000u;
		while ((g_qspi->SR & QUADSPI_SR_FTF) == 0u &&
		       ((g_qspi->SR >> QUADSPI_SR_FLEVEL_Pos) & 0x3Fu) == 0u) {
			if (g_qspi->SR & QUADSPI_SR_TEF) {
				g_qspi->FCR = QUADSPI_FCR_CTEF;
				return ULMK_ETIMEOUT;
			}
			if (guard-- == 0u)
				return ULMK_ETIMEOUT;
		}
		out[i] = *dr8;
	}
	return ULMK_OK;
}

static int do_cmd_write(uint8_t cmd, uint32_t imode)
{
	int ret;

	qspi_abort();
	g_qspi->DLR = 0u;
	ulmk_irq_ack(ULMK_BOARD_IRQ_QSPI);
	/* Indirect write, instruction only (no data). */
	g_qspi->CCR = (0u << QUADSPI_CCR_FMODE_Pos) |
		      (imode << QUADSPI_CCR_IMODE_Pos) |
		      ((uint32_t)cmd << QUADSPI_CCR_INSTRUCTION_Pos);
	ret = wait_tc();
	qspi_abort();
	return ret;
}

static int flash_reset(void)
{
	/* Exit QPI (0xFF on 4 lines) then SPI reset enable/reset. */
	(void)do_cmd_write(0xFFu, 3u);
	(void)do_cmd_write(0x66u, 1u);
	(void)do_cmd_write(0x99u, 1u);
	return ULMK_OK;
}

static int do_cmd_read(uint8_t cmd, uint8_t *out, size_t len)
{
	uint32_t w;
	uint32_t flevel;
	uint32_t i;
	int ret;

	if (!out || len == 0u || len > 16u)
		return ULMK_EINVAL;
	if (cmd == 0x9Fu) {
		(void)flash_reset();
		(void)do_cmd_write(0xABu, 1u); /* release power-down */
	}
	qspi_abort();
	g_qspi->DLR = (uint32_t)len - 1u;
	ulmk_irq_ack(ULMK_BOARD_IRQ_QSPI);
	g_qspi->CCR = QUADSPI_CCR_FMODE_0 |
		      (1u << QUADSPI_CCR_IMODE_Pos) |
		      (1u << QUADSPI_CCR_DMODE_Pos) |
		      ((uint32_t)cmd << QUADSPI_CCR_INSTRUCTION_Pos);
	ret = wait_tc();
	if (ret != ULMK_OK)
		return ret;
	flevel = (g_qspi->SR >> QUADSPI_SR_FLEVEL_Pos) & 0x3Fu;
	if (flevel == 0u)
		return ULMK_ETIMEOUT;
	/* Word pop for small xfers (FIFO endian = byte0 in LSB). */
	if (len <= 4u) {
		w = g_qspi->DR;
		for (i = 0u; i < len; i++)
			out[i] = (uint8_t)((w >> (i * 8u)) & 0xFFu);
		g_qspi->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF;
		return ULMK_OK;
	}
	ret = qspi_drain(out, len);
	g_qspi->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF;
	return ret;
}

static int do_read(uint32_t addr, uint8_t *out, size_t len)
{
	int ret;

	if (!out || len == 0u || len > 16u)
		return ULMK_EINVAL;
	qspi_abort();
	g_qspi->DLR = (uint32_t)len - 1u;
	/* Fast read 0x0B, 1-1-1, 8 dummy, 24-bit addr */
	g_qspi->CCR = QUADSPI_CCR_FMODE_0 |
		      (1u << QUADSPI_CCR_IMODE_Pos) |
		      (1u << QUADSPI_CCR_ADMODE_Pos) |
		      (2u << QUADSPI_CCR_ADSIZE_Pos) |
		      (1u << QUADSPI_CCR_DMODE_Pos) |
		      (8u << QUADSPI_CCR_DCYC_Pos) |
		      (0x0Bu << QUADSPI_CCR_INSTRUCTION_Pos);
	g_qspi->AR = addr;
	ret = wait_tc();
	if (ret != ULMK_OK)
		return ret;
	return qspi_drain(out, len);
}

static void pack_reply(ulmk_msg_t *reply, const uint8_t *data, size_t len)
{
	size_t i;

	reply->words[2] = 0u;
	reply->words[3] = 0u;
	reply->words[4] = 0u;
	reply->words[5] = 0u;
	for (i = 0u; i < len && i < 16u; i++)
		reply->words[2u + (i / 4u)] |=
			(uint32_t)data[i] << ((i % 4u) * 8u);
}

static void qspi_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	size_t len;

	(void)arg;
	g_qspi = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_QSPI_BASE,
			      ULMK_BOARD_QSPI_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!g_qspi)
		return;
	qspi_pinmux_ll();
	qspi_hw_init();

	for (;;) {
		if (ulmk_ep_recv(g_qspi_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		len = (size_t)msg.words[1];
		if (msg.label == QSPI_MSG_CMD_READ && len > 0u && len <= 16u) {
			reply.words[0] = (uint32_t)do_cmd_read(
				(uint8_t)msg.words[0], g_buf, len);
			if ((int)(int32_t)reply.words[0] == ULMK_OK)
				pack_reply(&reply, g_buf, len);
			reply.words[1] = g_qspi->SR;
			reply.words[5] = g_qspi->CR;
		} else if (msg.label == QSPI_MSG_READ && len > 0u &&
			   len <= 16u) {
			reply.words[0] = (uint32_t)do_read(msg.words[0], g_buf,
							   len);
			if ((int)(int32_t)reply.words[0] == ULMK_OK)
				pack_reply(&reply, g_buf, len);
			reply.words[1] = g_qspi->SR;
			reply.words[5] = g_qspi->CR;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t qspi_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (n != 0u || g_qspi_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_qspi_ep = ulmk_ep_create();
	g_qspi_notif = ulmk_notif_create();
	if (g_qspi_ep == ULMK_EP_INVALID || g_qspi_notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;
	if (ulmk_irq_bind_hw(ULMK_BOARD_IRQ_QSPI, g_qspi_notif, QSPI_NOTIF_TC,
			     ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_QSPI)) !=
	    ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_QSPI) != ULMK_OK)
		return ULMK_TID_INVALID;
	attr.name = "qspi";
	attr.entry = qspi_server;
	attr.priority = 2u;
	attr.stack_size = QSPI_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
