/* SPDX-License-Identifier: MIT */
/*
 * AXI-PDMA (GDMA-AXI) — mem-to-mem and peri TX/RX for GPSPI.
 *
 * Register layout differs from AHB-PDMA: link address lives in LINK2, start
 * pulse in LINK1, and the controller only accepts buffers inside the programmed
 * intr/extr memory windows (IDF defaults cover L2MEM + MSPI).
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "gdma_axi.h"
#include "gdma_axi_internal.h"
#include "board_config.h"
#include "board_cache.h"

#define AXI_CH_MEM	0u
#define GDMA_AXI_SLOT_COUNT	5u

#define AXI_BASE	ULMK_BOARD_AXI_PDMA_BASE
#define AXI_STRIDE	0x68u

#define AXI_IN_INT_RAW(n)	(AXI_BASE + 0x000u + (n) * AXI_STRIDE)
#define AXI_IN_INT_ENA(n)	(AXI_BASE + 0x008u + (n) * AXI_STRIDE)
#define AXI_IN_INT_CLR(n)	(AXI_BASE + 0x00cu + (n) * AXI_STRIDE)
#define AXI_IN_CONF0(n)		(AXI_BASE + 0x010u + (n) * AXI_STRIDE)
#define AXI_IN_LINK1(n)		(AXI_BASE + 0x020u + (n) * AXI_STRIDE)
#define AXI_IN_LINK2(n)		(AXI_BASE + 0x024u + (n) * AXI_STRIDE)
#define AXI_IN_PERI_SEL(n)	(AXI_BASE + 0x044u + (n) * AXI_STRIDE)

#define AXI_OUT_INT_RAW(n)	(AXI_BASE + 0x138u + (n) * AXI_STRIDE)
#define AXI_OUT_INT_ENA(n)	(AXI_BASE + 0x140u + (n) * AXI_STRIDE)
#define AXI_OUT_INT_CLR(n)	(AXI_BASE + 0x144u + (n) * AXI_STRIDE)
#define AXI_OUT_CONF0(n)	(AXI_BASE + 0x148u + (n) * AXI_STRIDE)
#define AXI_OUT_LINK1(n)	(AXI_BASE + 0x158u + (n) * AXI_STRIDE)
#define AXI_OUT_LINK2(n)	(AXI_BASE + 0x15cu + (n) * AXI_STRIDE)
#define AXI_OUT_PERI_SEL(n)	(AXI_BASE + 0x17cu + (n) * AXI_STRIDE)

#define AXI_INTR_MEM_START	(AXI_BASE + 0x27cu)
#define AXI_INTR_MEM_END	(AXI_BASE + 0x280u)
#define AXI_EXTR_MEM_START	(AXI_BASE + 0x284u)
#define AXI_EXTR_MEM_END	(AXI_BASE + 0x288u)
#define AXI_MISC_CONF		(AXI_BASE + 0x2a8u)

#define IN_RST			(1u << 0)
#define MEM_TRANS_EN		(1u << 2)
#define INDSCR_BURST_EN		(1u << 9)
#define OUT_RST			(1u << 0)
#define OUT_EOF_MODE		(1u << 3)
#define OUTDSCR_BURST_EN	(1u << 10)

#define INLINK_START		(1u << 2)
#define OUTLINK_START		(1u << 1)

#define IN_INT_SUC_EOF		(1u << 1)
#define IN_INT_DSCR_ERR		(1u << 3)
#define OUT_INT_EOF		(1u << 1)
#define OUT_INT_DSCR_ERR	(1u << 2)

#define MISC_AXIM_RST_WR	(1u << 0)
#define MISC_AXIM_RST_RD	(1u << 1)
#define MISC_CLK_EN		(1u << 4)

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL1	(HP_CLKRST + 0x18u)
#define HP_RST_EN1		(HP_CLKRST + 0xc4u)
#define AXI_PDMA_SYS_EN		(1u << 4)
#define RST_EN_AXI_PDMA		(1u << 2)

struct dma_desc {
	uint32_t dw0;
	uint32_t buf;
	uint32_t next;
};

#define DESC_SUC_EOF		(1u << 30)
#define DESC_OWNER_DMA		(1u << 31)
#define DESC_MAX_LEN		0xFFFu
#define DESC_NC_OFFSET		0x40000000u

#define GDMA_AXI_NOTIF	0u
#define GDMA_AXI_WAIT_MS	100u

struct gdma_axi_slot {
	ulmk_notif_t	notif;
	uint8_t		irq;
	uint8_t		hw_ch;
	uint8_t		is_tx;
	uint8_t		open;
	uint8_t		periph;
	void		*buf;
	uint32_t	len;
	struct dma_desc	desc __attribute__((aligned(16)));
};

static uint8_t g_hw;
static struct gdma_axi_slot g_slot[GDMA_AXI_SLOT_COUNT];
/* Mem-to-mem reuses CH0 OUT descriptor alongside slot0 IN. */
static struct dma_desc g_mem_out_desc __attribute__((aligned(16)));

ulmk_ep_t g_gdma_axi_eps[GDMA_AXI_MAX_INST] = { ULMK_EP_INVALID };

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static inline volatile struct dma_desc *desc_nc(struct dma_desc *d)
{
	return (volatile struct dma_desc *)((uintptr_t)d + DESC_NC_OFFSET);
}

static int wait_in_done(uint8_t hw_ch, ulmk_notif_t notif, uint8_t irq)
{
	uint32_t bits = 0u;
	int rc;

	(void)ulmk_irq_ack(irq);
	wr(AXI_IN_INT_ENA(hw_ch), IN_INT_SUC_EOF | IN_INT_DSCR_ERR);
	rc = ulmk_notif_wait_timeout(notif, 1u << GDMA_AXI_NOTIF, &bits,
				     GDMA_AXI_WAIT_MS);
	wr(AXI_IN_INT_ENA(hw_ch), 0u);
	return rc;
}

static int wait_out_done(uint8_t hw_ch, ulmk_notif_t notif, uint8_t irq)
{
	uint32_t bits = 0u;
	int rc;

	(void)ulmk_irq_ack(irq);
	wr(AXI_OUT_INT_ENA(hw_ch), OUT_INT_EOF | OUT_INT_DSCR_ERR);
	rc = ulmk_notif_wait_timeout(notif, 1u << GDMA_AXI_NOTIF, &bits,
				     GDMA_AXI_WAIT_MS);
	wr(AXI_OUT_INT_ENA(hw_ch), 0u);
	return rc;
}

static int gdma_axi_hw_init(void)
{
	uint32_t v;

	v = rd(HP_SOC_CLK_CTRL1);
	wr(HP_SOC_CLK_CTRL1, v | AXI_PDMA_SYS_EN);
	if ((rd(HP_SOC_CLK_CTRL1) & AXI_PDMA_SYS_EN) == 0u) {
		g_hw = 0u;
		return ULMK_ENOTSUP;
	}

	v = rd(HP_RST_EN1);
	wr(HP_RST_EN1, v | RST_EN_AXI_PDMA);
	wr(HP_RST_EN1, v & ~RST_EN_AXI_PDMA);

	/* IDF axi_dma_ll_set_default_memory_range */
	wr(AXI_INTR_MEM_START, 0x4FC00000u);
	wr(AXI_INTR_MEM_END, 0x4FFC0000u);
	wr(AXI_EXTR_MEM_START, 0x40000000u);
	wr(AXI_EXTR_MEM_END, 0x4C000000u);

	wr(AXI_MISC_CONF, MISC_AXIM_RST_RD | MISC_AXIM_RST_WR | MISC_CLK_EN);
	wr(AXI_MISC_CONF, MISC_CLK_EN);

	g_hw = 1u;
	return ULMK_OK;
}

static int hw_channel_open(uint8_t slot, uint32_t mux)
{
	uint8_t ch;

	if (slot >= GDMA_AXI_SLOT_COUNT)
		return ULMK_EINVAL;
	if (mux != GDMA_AXI_PERIPH_NONE && mux > 0x3Fu)
		return ULMK_EINVAL;
	if (!g_hw)
		return ULMK_ENOTSUP;
	if (g_slot[slot].notif == ULMK_NOTIF_INVALID)
		return ULMK_ENOTSUP;
	if (slot == GDMA_AXI_SLOT_MEM && mux != GDMA_AXI_PERIPH_NONE)
		return ULMK_EINVAL;
	if (slot != GDMA_AXI_SLOT_MEM && mux == GDMA_AXI_PERIPH_NONE)
		return ULMK_EINVAL;

	g_slot[slot].periph = (uint8_t)mux;
	g_slot[slot].open = 1u;

	ch = g_slot[slot].hw_ch;
	if (slot != GDMA_AXI_SLOT_MEM) {
		if (g_slot[slot].is_tx)
			wr(AXI_OUT_PERI_SEL(ch), mux & 0x3Fu);
		else
			wr(AXI_IN_PERI_SEL(ch), mux & 0x3Fu);
	}

	return ULMK_OK;
}

static int hw_rx_arm(uint8_t slot, void *dst, uint32_t len)
{
	volatile struct dma_desc *in;
	uint8_t ch;

	if (slot >= GDMA_AXI_SLOT_COUNT || !dst || len == 0u)
		return ULMK_EINVAL;
	if (slot == GDMA_AXI_SLOT_MEM || g_slot[slot].is_tx)
		return ULMK_EINVAL;
	if (!g_hw || !g_slot[slot].open)
		return ULMK_ENOTSUP;
	if (len > DESC_MAX_LEN)
		return ULMK_EINVAL;

	ch = g_slot[slot].hw_ch;
	in = desc_nc(&g_slot[slot].desc);
	in->dw0 = (len & DESC_MAX_LEN) | DESC_OWNER_DMA;
	in->buf = (uint32_t)(uintptr_t)dst;
	in->next = 0u;

	g_slot[slot].buf = dst;
	g_slot[slot].len = len;

	wr(AXI_IN_CONF0(ch), IN_RST);
	wr(AXI_IN_CONF0(ch), INDSCR_BURST_EN);
	wr(AXI_IN_PERI_SEL(ch), g_slot[slot].periph & 0x3Fu);
	wr(AXI_IN_INT_CLR(ch), 0xFFFFFFFFu);
	wr(AXI_IN_LINK2(ch), (uint32_t)(uintptr_t)&g_slot[slot].desc);
	wr(AXI_IN_LINK1(ch), INLINK_START);
	return ULMK_OK;
}

static int hw_rx_wait(uint8_t slot)
{
	volatile struct dma_desc *in;
	uint32_t raw;
	uint32_t got;
	uint8_t ch;

	if (slot >= GDMA_AXI_SLOT_COUNT)
		return ULMK_EINVAL;
	if (slot == GDMA_AXI_SLOT_MEM || g_slot[slot].is_tx)
		return ULMK_EINVAL;
	if (!g_slot[slot].buf)
		return ULMK_EINVAL;
	ch = g_slot[slot].hw_ch;

	if (wait_in_done(ch, g_slot[slot].notif, g_slot[slot].irq) !=
	    ULMK_OK) {
		wr(AXI_IN_INT_CLR(ch), 0xFFFFFFFFu);
		g_slot[slot].buf = NULL;
		return ULMK_ETIMEOUT;
	}

	raw = rd(AXI_IN_INT_RAW(ch));
	wr(AXI_IN_INT_CLR(ch), 0xFFFFFFFFu);
	in = desc_nc(&g_slot[slot].desc);
	got = (in->dw0 >> 12) & DESC_MAX_LEN;
	if (got > g_slot[slot].len)
		got = g_slot[slot].len;

	board_dcache_invalidate(g_slot[slot].buf, g_slot[slot].len);
	g_slot[slot].buf = NULL;

	if ((raw & IN_INT_DSCR_ERR) != 0u)
		return ULMK_EINVAL;
	if ((raw & IN_INT_SUC_EOF) == 0u)
		return ULMK_ETIMEOUT;
	return (int)got;
}

static int hw_tx_arm(uint8_t slot, const void *src, uint32_t len)
{
	volatile struct dma_desc *out;
	uint8_t ch;

	if (slot >= GDMA_AXI_SLOT_COUNT || !src || len == 0u)
		return ULMK_EINVAL;
	if (slot == GDMA_AXI_SLOT_MEM || !g_slot[slot].is_tx)
		return ULMK_EINVAL;
	if (!g_hw || !g_slot[slot].open)
		return ULMK_ENOTSUP;
	if (len > DESC_MAX_LEN)
		return ULMK_EINVAL;

	ch = g_slot[slot].hw_ch;
	board_dcache_clean(src, len);

	out = desc_nc(&g_slot[slot].desc);
	out->dw0 = (len & DESC_MAX_LEN) | ((len & DESC_MAX_LEN) << 12) |
		   DESC_SUC_EOF | DESC_OWNER_DMA;
	out->buf = (uint32_t)(uintptr_t)src;
	out->next = 0u;

	g_slot[slot].buf = (void *)(uintptr_t)src;
	g_slot[slot].len = len;

	wr(AXI_OUT_CONF0(ch), OUT_RST);
	wr(AXI_OUT_CONF0(ch), OUT_EOF_MODE | OUTDSCR_BURST_EN);
	wr(AXI_OUT_PERI_SEL(ch), g_slot[slot].periph & 0x3Fu);
	wr(AXI_OUT_INT_CLR(ch), 0xFFFFFFFFu);
	wr(AXI_OUT_LINK2(ch), (uint32_t)(uintptr_t)&g_slot[slot].desc);
	wr(AXI_OUT_LINK1(ch), OUTLINK_START);
	return ULMK_OK;
}

static int hw_tx_wait(uint8_t slot)
{
	uint32_t raw;
	uint8_t ch;

	if (slot >= GDMA_AXI_SLOT_COUNT)
		return ULMK_EINVAL;
	if (slot == GDMA_AXI_SLOT_MEM || !g_slot[slot].is_tx)
		return ULMK_EINVAL;
	if (!g_slot[slot].buf)
		return ULMK_EINVAL;
	ch = g_slot[slot].hw_ch;

	if (wait_out_done(ch, g_slot[slot].notif, g_slot[slot].irq) !=
	    ULMK_OK) {
		wr(AXI_OUT_INT_CLR(ch), 0xFFFFFFFFu);
		g_slot[slot].buf = NULL;
		return ULMK_ETIMEOUT;
	}

	raw = rd(AXI_OUT_INT_RAW(ch));
	wr(AXI_OUT_INT_CLR(ch), 0xFFFFFFFFu);
	g_slot[slot].buf = NULL;

	if ((raw & OUT_INT_DSCR_ERR) != 0u)
		return ULMK_EINVAL;
	if ((raw & OUT_INT_EOF) == 0u)
		return ULMK_ETIMEOUT;
	return ULMK_OK;
}

static int dma_memcpy_hw(void *dst, const void *src, uint32_t len)
{
	const uint8_t n = AXI_CH_MEM;
	volatile struct dma_desc *out = desc_nc(&g_mem_out_desc);
	volatile struct dma_desc *in = desc_nc(&g_slot[GDMA_AXI_SLOT_MEM].desc);
	uint32_t raw;

	board_dcache_clean(src, len);

	out->dw0 = (len & DESC_MAX_LEN) | ((len & DESC_MAX_LEN) << 12) |
		   DESC_SUC_EOF | DESC_OWNER_DMA;
	out->buf = (uint32_t)(uintptr_t)src;
	out->next = 0u;

	in->dw0 = (len & DESC_MAX_LEN) | DESC_OWNER_DMA;
	in->buf = (uint32_t)(uintptr_t)dst;
	in->next = 0u;

	wr(AXI_IN_CONF0(n), IN_RST);
	wr(AXI_OUT_CONF0(n), OUT_RST);
	wr(AXI_IN_CONF0(n), MEM_TRANS_EN | INDSCR_BURST_EN);
	wr(AXI_OUT_CONF0(n), OUT_EOF_MODE | OUTDSCR_BURST_EN);
	wr(AXI_IN_PERI_SEL(n), GDMA_AXI_PERIPH_NONE);
	wr(AXI_OUT_PERI_SEL(n), GDMA_AXI_PERIPH_NONE);

	wr(AXI_IN_INT_CLR(n), 0xFFFFFFFFu);
	wr(AXI_OUT_INT_CLR(n), 0xFFFFFFFFu);

	wr(AXI_OUT_LINK2(n), (uint32_t)(uintptr_t)&g_mem_out_desc);
	wr(AXI_IN_LINK2(n),
	   (uint32_t)(uintptr_t)&g_slot[GDMA_AXI_SLOT_MEM].desc);

	wr(AXI_IN_LINK1(n), INLINK_START);
	wr(AXI_OUT_LINK1(n), OUTLINK_START);

	if (wait_in_done(n, g_slot[GDMA_AXI_SLOT_MEM].notif,
			 g_slot[GDMA_AXI_SLOT_MEM].irq) != ULMK_OK) {
		wr(AXI_IN_INT_CLR(n), 0xFFFFFFFFu);
		return ULMK_ETIMEOUT;
	}

	raw = rd(AXI_IN_INT_RAW(n));
	wr(AXI_IN_INT_CLR(n), 0xFFFFFFFFu);

	if ((raw & IN_INT_DSCR_ERR) != 0u)
		return ULMK_EINVAL;
	if ((raw & IN_INT_SUC_EOF) == 0u)
		return ULMK_ETIMEOUT;

	board_dcache_invalidate(dst, len);
	return ULMK_OK;
}

static int hw_memcpy(void *dst, const void *src, uint32_t len)
{
	if (!dst || !src)
		return ULMK_EINVAL;
	if (!g_hw)
		return ULMK_ENOTSUP;
	if (len == 0u)
		return ULMK_OK;
	if (len > DESC_MAX_LEN)
		return ULMK_EINVAL;
	return dma_memcpy_hw(dst, src, len);
}

static void gdma_axi_server(void *arg)
{
	ulmk_ep_t ep = g_gdma_axi_eps[(uint8_t)(uintptr_t)arg];
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;

		switch (msg.label) {
		case GDMA_AXI_MSG_OPEN:
			reply.words[0] = (uint32_t)hw_channel_open(
				(uint8_t)msg.words[0], msg.words[1]);
			break;
		case GDMA_AXI_MSG_MEMCPY:
			reply.words[0] = (uint32_t)hw_memcpy(
				(void *)(uintptr_t)msg.words[0],
				(const void *)(uintptr_t)msg.words[1],
				msg.words[2]);
			break;
		case GDMA_AXI_MSG_RX_ARM:
			reply.words[0] = (uint32_t)hw_rx_arm(
				(uint8_t)msg.words[0],
				(void *)(uintptr_t)msg.words[1],
				msg.words[2]);
			break;
		case GDMA_AXI_MSG_RX_WAIT:
			reply.words[0] =
				(uint32_t)hw_rx_wait((uint8_t)msg.words[0]);
			break;
		case GDMA_AXI_MSG_TX_ARM:
			reply.words[0] = (uint32_t)hw_tx_arm(
				(uint8_t)msg.words[0],
				(const void *)(uintptr_t)msg.words[1],
				msg.words[2]);
			break;
		case GDMA_AXI_MSG_TX_WAIT:
			reply.words[0] =
				(uint32_t)hw_tx_wait((uint8_t)msg.words[0]);
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t gdma_axi_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	uint32_t clic_reg;
	ulmk_tid_t tid;
	static const struct {
		uint8_t irq;
		uint8_t clic;
		uint8_t hw_ch;
		uint8_t is_tx;
	} bind[GDMA_AXI_SLOT_COUNT] = {
		{ ULMK_BOARD_IRQ_AXI_PDMA_IN_CH0,
		  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH0, AXI_CH_MEM, 0u },
		{ ULMK_BOARD_IRQ_AXI_PDMA_IN_CH1,
		  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH1, 1u, 0u },
		{ ULMK_BOARD_IRQ_AXI_PDMA_OUT_CH1,
		  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_OUT_CH1, 1u, 1u },
		{ ULMK_BOARD_IRQ_AXI_PDMA_IN_CH2,
		  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_IN_CH2, 2u, 0u },
		{ ULMK_BOARD_IRQ_AXI_PDMA_OUT_CH2,
		  ULMK_BOARD_CLIC_IRQ_AXI_PDMA_OUT_CH2, 2u, 1u },
	};
	uint32_t i;

	if (n >= GDMA_AXI_MAX_INST)
		return ULMK_TID_INVALID;
	if (g_gdma_axi_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_gdma_axi_eps[n] = ulmk_ep_create();
	if (g_gdma_axi_eps[n] == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	if (gdma_axi_hw_init() != ULMK_OK)
		return ULMK_TID_INVALID;

	for (i = 0u; i < GDMA_AXI_SLOT_COUNT; i++) {
		g_slot[i].periph = GDMA_AXI_PERIPH_NONE;
		g_slot[i].hw_ch = bind[i].hw_ch;
		g_slot[i].is_tx = bind[i].is_tx;
		g_slot[i].notif = ulmk_notif_create();
		if (g_slot[i].notif == ULMK_NOTIF_INVALID)
			return ULMK_TID_INVALID;
		g_slot[i].irq = bind[i].irq;
		clic_reg = ULMK_BOARD_CLIC_BASE + 0x1000u +
			   (uint32_t)bind[i].clic * 4u;
		if (ulmk_irq_bind_hw(g_slot[i].irq, g_slot[i].notif,
				     GDMA_AXI_NOTIF, clic_reg) != ULMK_OK)
			return ULMK_TID_INVALID;
		if (ulmk_irq_enable(g_slot[i].irq) != ULMK_OK)
			return ULMK_TID_INVALID;
	}

	attr.name = "gdma_axi";
	attr.entry = gdma_axi_server;
	attr.arg = (void *)(uintptr_t)n;
	attr.priority = 2u;
	attr.stack_size = 2048u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH | ULMK_CAP_IRQ);
	return tid;
}
