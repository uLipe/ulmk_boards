/* SPDX-License-Identifier: MIT */
/*
 * I2C3 master — DMA via drivers/dma (STR1 TX / STR2 RX) + EV notif.
 *
 * Combined write-then-read uses STOP between phases (not RELOAD+restart).
 * FT5x06 and most slaves accept that; H7 RELOAD+DMA restart left the bus
 * BUSY/TXIS without ever entering master-receive.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <dma.h>
#include "board_config.h"
#include "board_ll.h"
#include "i2c_internal.h"
#include "pinmux_internal.h"

#define I2C_STACK_SIZE		2048u
/* TIMINGR: Fast-Mode 400 kHz @ I2CCLK = PCLK1 120 MHz (CubeH7). */
#define I2C_TIMING_400K		0x10C0ECFFu
#define I2C_RXDR_OFF		0x24u
#define I2C_TXDR_OFF		0x28u
#define I2C_DMA_WAIT_MS		500u

#define I2C_TXDR_PHYS	((uintptr_t)ULMK_BOARD_I2C3_BASE + I2C_TXDR_OFF)
#define I2C_RXDR_PHYS	((uintptr_t)ULMK_BOARD_I2C3_BASE + I2C_RXDR_OFF)

ulmk_ep_t g_i2c_eps[I2C_MAX];
static ulmk_notif_t g_i2c_notif __attribute__((section(".user_bss")));
static I2C_TypeDef *g_i2c __attribute__((section(".user_bss")));
static uint8_t g_tx_buf[I2C_XFER_MAX] __attribute__((section(".user_bss")));
static uint8_t g_rx_buf[I2C_XFER_MAX] __attribute__((section(".user_bss")));
static uint32_t g_bitrate __attribute__((section(".user_bss")));

static void i2c_pinmux(void)
{
	pinmux_cfg_t cfg;

	cfg.port = ULMK_BOARD_I2C_SCL_PORT;
	cfg.pin = ULMK_BOARD_I2C_SCL_PIN;
	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_UP;
	cfg.alt = ULMK_BOARD_I2C_AF;
	cfg.flags = PINMUX_F_OPENDRAIN;
	(void)pinmux_apply(&cfg);

	cfg.port = ULMK_BOARD_I2C_SDA_PORT;
	cfg.pin = ULMK_BOARD_I2C_SDA_PIN;
	(void)pinmux_apply(&cfg);
}

static void i2c_hw_init(void)
{
	g_i2c->CR1 = 0u;
	g_i2c->TIMINGR = (g_bitrate == ULMK_BOARD_I2C_BITRATE_HZ) ?
			 I2C_TIMING_400K : I2C_TIMING_400K;
	g_i2c->CR1 = I2C_CR1_PE | I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN |
		     I2C_CR1_TCIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE |
		     I2C_CR1_ERRIE;
}

static void i2c_bus_recover(void)
{
	uint32_t i;

	if (!(g_i2c->ISR & I2C_ISR_BUSY)) {
		g_i2c->ICR = 0x3F38u;
		return;
	}
	g_i2c->CR2 |= I2C_CR2_STOP;
	for (i = 0u; i < 50u; i++) {
		if (g_i2c->ISR & I2C_ISR_STOPF)
			break;
		(void)ulmk_sleep_ms(1u);
	}
	g_i2c->ICR = 0x3F38u;
	if (g_i2c->ISR & I2C_ISR_BUSY) {
		g_i2c->CR1 &= ~I2C_CR1_PE;
		(void)ulmk_sleep_ms(1u);
		g_i2c->CR1 |= I2C_CR1_PE | I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN |
			      I2C_CR1_TCIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE |
			      I2C_CR1_ERRIE;
	}
}

static int wait_ev(void)
{
	uint32_t bits = 0u;
	int ret;

	ulmk_irq_ack(ULMK_BOARD_IRQ_I2C3_EV);
	ret = ulmk_notif_wait(g_i2c_notif, 1u << I2C_NOTIF_EV, &bits);
	ulmk_irq_ack(ULMK_BOARD_IRQ_I2C3_EV);
	return ret;
}

static int check_nack(void)
{
	if (g_i2c->ISR & I2C_ISR_NACKF) {
		g_i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
		return ULMK_ESRCH;
	}
	return ULMK_OK;
}

static int wait_stop_or_nack(void)
{
	int ret;

	for (;;) {
		if (g_i2c->ISR & I2C_ISR_NACKF)
			return check_nack();
		if (g_i2c->ISR & I2C_ISR_STOPF) {
			g_i2c->ICR = I2C_ICR_STOPCF;
			return ULMK_OK;
		}
		ret = wait_ev();
		if (ret != ULMK_OK)
			return ret;
	}
}

static int i2c_do_write(uint8_t addr7, const uint8_t *data, size_t len)
{
	uint32_t i;
	int ret;

	if (len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	for (i = 0u; i < len; i++)
		g_tx_buf[i] = data ? data[i] : 0u;

	g_i2c->ICR = 0x3F38u;
	if (len == 0u) {
		g_i2c->CR2 = ((uint32_t)addr7 << 1) | I2C_CR2_START |
			     I2C_CR2_AUTOEND;
		ret = wait_stop_or_nack();
		if (ret != ULMK_OK)
			i2c_bus_recover();
		return ret;
	}

	ret = dma_arm(ULMK_BOARD_DMA_SLOT_I2C_TX, I2C_TXDR_PHYS, g_tx_buf,
		      len);
	if (ret != ULMK_OK)
		return ret;
	g_i2c->CR2 = ((uint32_t)addr7 << 1) |
		     (((uint32_t)len << I2C_CR2_NBYTES_Pos) & I2C_CR2_NBYTES) |
		     I2C_CR2_START | I2C_CR2_AUTOEND;
	ret = dma_wait(ULMK_BOARD_DMA_SLOT_I2C_TX, NULL, 0u, I2C_DMA_WAIT_MS);
	if (ret != ULMK_OK) {
		i2c_bus_recover();
		return ret;
	}
	ret = wait_stop_or_nack();
	if (ret != ULMK_OK)
		i2c_bus_recover();
	return ret;
}

static int i2c_do_read(uint8_t addr7, uint8_t *data, size_t len)
{
	uint32_t i;
	int ret;

	if (!data || len == 0u || len > I2C_XFER_MAX)
		return ULMK_EINVAL;

	g_i2c->ICR = 0x3F38u;
	ret = dma_arm(ULMK_BOARD_DMA_SLOT_I2C_RX, I2C_RXDR_PHYS, NULL, len);
	if (ret != ULMK_OK)
		return ret;
	g_i2c->CR2 = ((uint32_t)addr7 << 1) | I2C_CR2_RD_WRN |
		     (((uint32_t)len << I2C_CR2_NBYTES_Pos) & I2C_CR2_NBYTES) |
		     I2C_CR2_START | I2C_CR2_AUTOEND;
	ret = dma_wait(ULMK_BOARD_DMA_SLOT_I2C_RX, g_rx_buf, len,
		       I2C_DMA_WAIT_MS);
	if (ret != ULMK_OK) {
		i2c_bus_recover();
		return ret;
	}
	ret = wait_stop_or_nack();
	if (ret != ULMK_OK) {
		i2c_bus_recover();
		return ret;
	}
	for (i = 0u; i < len; i++)
		data[i] = g_rx_buf[i];
	return ULMK_OK;
}

static int i2c_do_writeread(uint8_t addr7, const uint8_t *w, size_t wlen,
			    uint8_t *r, size_t rlen)
{
	int ret;

	if (!w || !r || wlen == 0u || rlen == 0u ||
	    wlen > I2C_XFER_MAX || rlen > I2C_XFER_MAX)
		return ULMK_EINVAL;

	ret = i2c_do_write(addr7, w, wlen);
	if (ret != ULMK_OK)
		return ret;
	return i2c_do_read(addr7, r, rlen);
}

static void pack_reply(ulmk_msg_t *reply, const uint8_t *data, size_t len)
{
	size_t i;

	reply->words[2] = 0u;
	reply->words[3] = 0u;
	reply->words[4] = 0u;
	reply->words[5] = 0u;
	for (i = 0u; i < len && i < I2C_XFER_MAX; i++)
		reply->words[2u + (i / 4u)] |=
			(uint32_t)data[i] << ((i % 4u) * 8u);
}

static void i2c_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t wbuf[I2C_XFER_MAX];
	uint8_t rbuf[I2C_XFER_MAX];
	size_t wlen;
	size_t rlen;
	size_t i;

	(void)arg;
	g_i2c = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_I2C3_BASE,
			     ULMK_BOARD_I2C_MAP_SIZE,
			     ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!g_i2c)
		return;
	i2c_pinmux();
	i2c_hw_init();

	for (;;) {
		if (ulmk_ep_recv(g_i2c_eps[0], &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		if (msg.label == I2C_MSG_PROBE) {
			reply.words[0] = (uint32_t)i2c_do_write(
				(uint8_t)msg.words[0], NULL, 0u);
		} else if (msg.label == I2C_MSG_WRITE) {
			wlen = (size_t)msg.words[1];
			for (i = 0u; i < wlen && i < I2C_XFER_MAX; i++)
				wbuf[i] = (uint8_t)((msg.words[2u + (i / 4u)] >>
						     ((i % 4u) * 8u)) & 0xFFu);
			reply.words[0] = (uint32_t)i2c_do_write(
				(uint8_t)msg.words[0], wbuf, wlen);
		} else if (msg.label == I2C_MSG_READ) {
			rlen = (size_t)msg.words[1];
			reply.words[0] = (uint32_t)i2c_do_read(
				(uint8_t)msg.words[0], rbuf, rlen);
			if ((int)(int32_t)reply.words[0] == ULMK_OK)
				pack_reply(&reply, rbuf, rlen);
		} else if (msg.label == I2C_MSG_WRITEREAD) {
			wlen = (size_t)(msg.words[1] & 0xFFu);
			rlen = (size_t)((msg.words[1] >> 8) & 0xFFu);
			for (i = 0u; i < wlen && i < 4u; i++)
				wbuf[i] = (uint8_t)((msg.words[2] >>
						     (i * 8u)) & 0xFFu);
			reply.words[0] = (uint32_t)i2c_do_writeread(
				(uint8_t)msg.words[0], wbuf, wlen, rbuf, rlen);
			if ((int)(int32_t)reply.words[0] == ULMK_OK)
				pack_reply(&reply, rbuf, rlen);
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t i2c_init(uint8_t n, uint32_t bitrate_hz)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	int ret;

	if (n != 0u || g_i2c_eps[0] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	if (dma_init() == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;

	ret = dma_channel_open(ULMK_BOARD_DMA_SLOT_I2C_TX,
			       ULMK_BOARD_DMAMUX_I2C3_TX,
			       DMA_FLAG_DIR_M2P | DMA_FLAG_MINC);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = dma_channel_open(ULMK_BOARD_DMA_SLOT_I2C_RX,
			       ULMK_BOARD_DMAMUX_I2C3_RX, DMA_FLAG_MINC);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	g_bitrate = bitrate_hz ? bitrate_hz : ULMK_BOARD_I2C_BITRATE_HZ;
	g_i2c_eps[0] = ulmk_ep_create();
	g_i2c_notif = ulmk_notif_create();
	if (g_i2c_eps[0] == ULMK_EP_INVALID ||
	    g_i2c_notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;

	if (ulmk_irq_bind_hw(ULMK_BOARD_IRQ_I2C3_EV, g_i2c_notif, I2C_NOTIF_EV,
			     ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_I2C3_EV)) !=
	    ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_I2C3_EV) != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name = "i2c";
	attr.entry = i2c_server;
	attr.priority = 2u;
	attr.stack_size = I2C_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
