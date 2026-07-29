/* SPDX-License-Identifier: MIT */
/*
 * AHB-PDMA memory-to-memory.
 *
 * The controller loops TX channel N back into RX channel N when
 * IN_CONF0.MEM_TRANS_EN is set: the outlink descriptor supplies the source and
 * the inlink descriptor the destination.  Register offsets and bit positions
 * below follow the ESP32-P4 AHB_DMA block; the earlier revision of this file
 * had them wrong and was writing configuration values into interrupt
 * registers, so nothing ever moved.
 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "dma.h"
#include "dma_internal.h"
#include "board_config.h"
#include "board_cache.h"

/*
 * Channel 0 does memory-to-memory; channel 1 receives from a peripheral
 * (the ADC digital controller).  They are kept apart so a conversion frame
 * in flight is not disturbed by an unrelated copy.
 */
#define DMA_CH_MEM	0u
#define DMA_CH_PERI	1u
#define DMA_NCH		2u

#define PDMA_BASE	ULMK_BOARD_AHB_PDMA_BASE

/* Per-channel interrupt clusters: 4 registers each, RAW/ST/ENA/CLR. */
#define PDMA_IN_INT_RAW(n)	(PDMA_BASE + 0x000u + (n) * 0x10u)
#define PDMA_IN_INT_ENA(n)	(PDMA_BASE + 0x008u + (n) * 0x10u)
#define PDMA_IN_INT_CLR(n)	(PDMA_BASE + 0x00cu + (n) * 0x10u)
#define PDMA_OUT_INT_RAW(n)	(PDMA_BASE + 0x030u + (n) * 0x10u)
#define PDMA_OUT_INT_CLR(n)	(PDMA_BASE + 0x03cu + (n) * 0x10u)

/* Channel cluster: RX half at +0x00, TX half at +0x60, stride 0xC0. */
#define PDMA_CH(n)		(PDMA_BASE + 0x070u + (n) * 0xc0u)
#define PDMA_IN_CONF0(n)	(PDMA_CH(n) + 0x00u)
#define PDMA_IN_LINK(n)		(PDMA_CH(n) + 0x10u)
#define PDMA_IN_PERI_SEL(n)	(PDMA_CH(n) + 0x30u)
#define PDMA_OUT_CONF0(n)	(PDMA_CH(n) + 0x60u)
#define PDMA_OUT_LINK(n)	(PDMA_CH(n) + 0x70u)

/* Descriptor head addresses live outside the channel cluster. */
#define PDMA_IN_LINK_ADDR(n)	(PDMA_BASE + 0x3acu + (n) * 4u)
#define PDMA_OUT_LINK_ADDR(n)	(PDMA_BASE + 0x3b8u + (n) * 4u)

#define IN_RST			(1u << 0)
#define INDSCR_BURST_EN		(1u << 2)
#define IN_DATA_BURST_EN	(1u << 3)
#define MEM_TRANS_EN		(1u << 4)
#define OUT_RST			(1u << 0)

/* IN_LINK carries an extra AUTO_RET bit, so its controls sit one bit up. */
#define INLINK_START		(1u << 2)
#define OUTLINK_START		(1u << 1)

#define IN_INT_SUC_EOF		(1u << 1)
#define IN_INT_DSCR_ERR		(1u << 3)

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL1	(HP_CLKRST + 0x18u)
#define HP_RST_EN1		(HP_CLKRST + 0xc4u)
#define AHB_PDMA_SYS_EN		(1u << 3)
#define RST_EN_AHB_PDMA		(1u << 1)

/*
 * dw0: size[11:0] | length[23:12] | err_eof[28] | suc_eof[30] | owner[31].
 * Three words only — a fourth would shift buffer and next out of place.
 */
struct dma_desc {
	uint32_t dw0;
	uint32_t buf;
	uint32_t next;
};

#define DESC_SUC_EOF		(1u << 30)
#define DESC_OWNER_DMA		(1u << 31)
#define DESC_MAX_LEN		0xFFFu

/*
 * Descriptors are fetched by the DMA, not by the CPU, so they are reached
 * through the non-cacheable alias of internal SRAM.  Writing them cached
 * would need a writeback on every transfer.
 */
#define DESC_NC_OFFSET		0x40000000u

#define DMA_NOTIF_IRQ	0u
/* Bounded: a wedged channel must not make the server unresponsive. */
#define DMA_WAIT_MS	100u

struct dma_ch {
	ulmk_notif_t	notif;
	uint8_t		irq;
	uint8_t		open;
	uint8_t		periph;		/* DMA_PERIPH_NONE = mem-to-mem */
	void		*rx_dst;	/* non-NULL while a receive is armed */
	uint32_t	rx_len;
	struct dma_desc	in_desc __attribute__((aligned(16)));
	struct dma_desc	out_desc __attribute__((aligned(16)));
};

static uint8_t g_hw;
static struct dma_ch g_ch[DMA_NCH];

ulmk_ep_t g_dma_eps[DMA_MAX_INST] = { ULMK_EP_INVALID };

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

/*
 * Completion is reported by the receiving channel in both directions.  Its
 * enable is kept off outside this window: the CLIC line follows the masked
 * status, so an armed-but-unserviced channel would keep re-entering the
 * handler.
 */
static int dma_wait_done(uint8_t n)
{
	uint32_t bits = 0u;
	int rc;

	(void)ulmk_irq_ack(g_ch[n].irq);
	wr(PDMA_IN_INT_ENA(n), IN_INT_SUC_EOF | IN_INT_DSCR_ERR);

	rc = ulmk_notif_wait_timeout(g_ch[n].notif, 1u << DMA_NOTIF_IRQ, &bits,
				     DMA_WAIT_MS);

	wr(PDMA_IN_INT_ENA(n), 0u);
	return rc;
}

static int dma_hw_init(void)
{
	uint32_t v;

	v = rd(HP_SOC_CLK_CTRL1);
	wr(HP_SOC_CLK_CTRL1, v | AHB_PDMA_SYS_EN);
	if ((rd(HP_SOC_CLK_CTRL1) & AHB_PDMA_SYS_EN) == 0u) {
		g_hw = 0u;
		return ULMK_ENOTSUP;
	}

	v = rd(HP_RST_EN1);
	wr(HP_RST_EN1, v | RST_EN_AHB_PDMA);
	wr(HP_RST_EN1, v & ~RST_EN_AHB_PDMA);

	g_hw = 1u;
	return ULMK_OK;
}

static int hw_channel_open(uint8_t slot, uint32_t mux)
{
	if (slot >= DMA_NCH)
		return ULMK_EINVAL;
	if (mux != DMA_PERIPH_NONE && mux > 0x3Fu)
		return ULMK_EINVAL;
	if (!g_hw)
		return ULMK_ENOTSUP;
	if (g_ch[slot].notif == ULMK_NOTIF_INVALID)
		return ULMK_ENOTSUP;

	g_ch[slot].periph = (uint8_t)mux;
	g_ch[slot].open = 1u;
	if (mux != DMA_PERIPH_NONE)
		wr(PDMA_IN_PERI_SEL(slot), mux & 0x3Fu);
	return ULMK_OK;
}

/*
 * Peripheral-to-memory: the source is the block selected by IN_PERI_SEL, so
 * only the inlink is programmed and MEM_TRANS_EN must stay clear.  The
 * peripheral decides where the frame ends; the descriptor only caps it.
 */
static int hw_rx_arm(uint8_t slot, void *dst, uint32_t len)
{
	volatile struct dma_desc *in;

	if (slot >= DMA_NCH || !dst || len == 0u)
		return ULMK_EINVAL;
	if (!g_hw || !g_ch[slot].open)
		return ULMK_ENOTSUP;
	if (g_ch[slot].periph == DMA_PERIPH_NONE)
		return ULMK_EINVAL;
	if (len > DESC_MAX_LEN)
		return ULMK_EINVAL;

	in = desc_nc(&g_ch[slot].in_desc);
	in->dw0 = (len & DESC_MAX_LEN) | DESC_OWNER_DMA;
	in->buf = (uint32_t)(uintptr_t)dst;
	in->next = 0u;

	g_ch[slot].rx_dst = dst;
	g_ch[slot].rx_len = len;

	wr(PDMA_IN_CONF0(slot), IN_RST);
	wr(PDMA_IN_CONF0(slot), INDSCR_BURST_EN | IN_DATA_BURST_EN);
	wr(PDMA_IN_PERI_SEL(slot), g_ch[slot].periph & 0x3Fu);
	wr(PDMA_IN_INT_CLR(slot), 0xFFFFFFFFu);

	wr(PDMA_IN_LINK_ADDR(slot), (uint32_t)(uintptr_t)&g_ch[slot].in_desc);
	wr(PDMA_IN_LINK(slot), INLINK_START);
	return ULMK_OK;
}

static int hw_rx_wait(uint8_t slot)
{
	volatile struct dma_desc *in;
	uint32_t raw;
	uint32_t got;

	if (slot >= DMA_NCH)
		return ULMK_EINVAL;
	if (!g_ch[slot].rx_dst)
		return ULMK_EINVAL;

	if (dma_wait_done(slot) != ULMK_OK) {
		wr(PDMA_IN_INT_CLR(slot), 0xFFFFFFFFu);
		g_ch[slot].rx_dst = NULL;
		return ULMK_ETIMEOUT;
	}

	raw = rd(PDMA_IN_INT_RAW(slot));
	wr(PDMA_IN_INT_CLR(slot), 0xFFFFFFFFu);
	in = desc_nc(&g_ch[slot].in_desc);
	got = (in->dw0 >> 12) & DESC_MAX_LEN;
	if (got > g_ch[slot].rx_len)
		got = g_ch[slot].rx_len;

	board_dcache_invalidate(g_ch[slot].rx_dst, g_ch[slot].rx_len);
	g_ch[slot].rx_dst = NULL;

	if ((raw & IN_INT_DSCR_ERR) != 0u)
		return ULMK_EINVAL;
	if ((raw & IN_INT_SUC_EOF) == 0u)
		return ULMK_ETIMEOUT;
	return (int)got;
}

static int dma_memcpy_hw(void *dst, const void *src, uint32_t len)
{
	const uint8_t n = DMA_CH_MEM;
	volatile struct dma_desc *out = desc_nc(&g_ch[n].out_desc);
	volatile struct dma_desc *in = desc_nc(&g_ch[n].in_desc);
	uint32_t raw;

	/* The source is produced by the CPU; the DMA reads it from memory. */
	board_dcache_clean(src, len);

	out->dw0 = (len & DESC_MAX_LEN) | ((len & DESC_MAX_LEN) << 12) |
		   DESC_SUC_EOF | DESC_OWNER_DMA;
	out->buf = (uint32_t)(uintptr_t)src;
	out->next = 0u;

	/* Inlink advertises capacity only; hardware fills in the length. */
	in->dw0 = (len & DESC_MAX_LEN) | DESC_OWNER_DMA;
	in->buf = (uint32_t)(uintptr_t)dst;
	in->next = 0u;

	wr(PDMA_IN_CONF0(n), IN_RST);
	wr(PDMA_OUT_CONF0(n), OUT_RST);
	wr(PDMA_IN_CONF0(n), MEM_TRANS_EN | INDSCR_BURST_EN |
			     IN_DATA_BURST_EN);
	wr(PDMA_OUT_CONF0(n), 0u);

	wr(PDMA_IN_INT_CLR(n), 0xFFFFFFFFu);
	wr(PDMA_OUT_INT_CLR(n), 0xFFFFFFFFu);

	wr(PDMA_OUT_LINK_ADDR(n), (uint32_t)(uintptr_t)&g_ch[n].out_desc);
	wr(PDMA_IN_LINK_ADDR(n), (uint32_t)(uintptr_t)&g_ch[n].in_desc);

	wr(PDMA_IN_LINK(n), INLINK_START);
	wr(PDMA_OUT_LINK(n), OUTLINK_START);

	/*
	 * Arming after the start is safe: the raw status latches, so a
	 * transfer that already finished raises the line as soon as the
	 * enable goes in.
	 */
	if (dma_wait_done(n) != ULMK_OK) {
		wr(PDMA_IN_INT_CLR(n), 0xFFFFFFFFu);
		return ULMK_ETIMEOUT;
	}

	raw = rd(PDMA_IN_INT_RAW(n));
	wr(PDMA_IN_INT_CLR(n), 0xFFFFFFFFu);

	/* Descriptor error: buffer outside the DMA's address range. */
	if ((raw & IN_INT_DSCR_ERR) != 0u)
		return ULMK_EINVAL;
	if ((raw & IN_INT_SUC_EOF) == 0u)
		return ULMK_ETIMEOUT;

	/* DMA wrote around the cache; drop stale lines. */
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

static void dma_server(void *arg)
{
	ulmk_ep_t ep = g_dma_eps[(uint8_t)(uintptr_t)arg];
	ulmk_msg_t msg, reply;
	ulmk_tid_t sender;

	for (;;) {
		if (ulmk_ep_recv(ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;

		switch (msg.label) {
		case DMA_MSG_OPEN:
			reply.words[0] = (uint32_t)hw_channel_open(
				(uint8_t)msg.words[0], msg.words[1]);
			break;
		case DMA_MSG_MEMCPY:
			reply.words[0] = (uint32_t)hw_memcpy(
				(void *)(uintptr_t)msg.words[0],
				(const void *)(uintptr_t)msg.words[1],
				msg.words[2]);
			break;
		case DMA_MSG_RX_ARM:
			reply.words[0] = (uint32_t)hw_rx_arm(
				(uint8_t)msg.words[0],
				(void *)(uintptr_t)msg.words[1],
				msg.words[2]);
			break;
		case DMA_MSG_RX_WAIT:
			reply.words[0] =
				(uint32_t)hw_rx_wait((uint8_t)msg.words[0]);
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t dma_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	uint32_t clic_reg;
	ulmk_tid_t tid;
	uint32_t i;

	if (n >= DMA_MAX_INST)
		return ULMK_TID_INVALID;
	if (g_dma_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_dma_eps[n] = ulmk_ep_create();
	if (g_dma_eps[n] == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	if (dma_hw_init() != ULMK_OK)
		return ULMK_TID_INVALID;

	for (i = 0u; i < DMA_NCH; i++) {
		g_ch[i].periph = DMA_PERIPH_NONE;
		g_ch[i].notif = ulmk_notif_create();
		if (g_ch[i].notif == ULMK_NOTIF_INVALID)
			return ULMK_TID_INVALID;
		g_ch[i].irq = (uint8_t)(ULMK_BOARD_IRQ_PDMA_IN_CH0 + i);
		clic_reg = ULMK_BOARD_CLIC_BASE + 0x1000u +
			   ((uint32_t)ULMK_BOARD_CLIC_IRQ_PDMA_IN_CH0 + i) * 4u;
		if (ulmk_irq_bind_hw(g_ch[i].irq, g_ch[i].notif,
				     DMA_NOTIF_IRQ, clic_reg) != ULMK_OK)
			return ULMK_TID_INVALID;
		if (ulmk_irq_enable(g_ch[i].irq) != ULMK_OK)
			return ULMK_TID_INVALID;
	}

	attr.name = "ahbdma";
	attr.entry = dma_server;
	attr.arg = (void *)(uintptr_t)n;
	attr.priority = 2u;
	attr.stack_size = 2048u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid != ULMK_TID_INVALID)
		ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH | ULMK_CAP_IRQ);
	return tid;
}
