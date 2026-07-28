/* SPDX-License-Identifier: MIT */
/*
 * DMA1 stream pool — one mmap of DMA+DMAMUX; worker per slot.
 * Clients copy via IPC (DMA_XFER_MAX); peripheral addr is programmed as PAR.
 *
 * Protocol: ARM enables the stream then the worker blocks on TC immediately
 * (signals ARMED so the client can kick the peripheral). WAIT joins that TC.
 * This avoids missing the IRQ when a short xfer finishes before WAIT is posted.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_cache.h"
#include "board_ll.h"
#include "dma_internal.h"

#define DMA_STACK_SIZE		1536u
#define DMA_DISP_STACK		2048u
#define DMA_DEFAULT_WAIT_MS	1000u

struct dma_slot {
	uint8_t open;
	uint8_t mux_req;
	uint32_t flags;
	uint8_t buf[DMA_XFER_MAX];
	ulmk_notif_t irq_notif;
	ulmk_notif_t job_notif;
	ulmk_notif_t done_notif;
	volatile uint32_t job;
	volatile uintptr_t periph;
	volatile uint32_t len;
	volatile uint32_t timeout_ms;
	volatile int result;
	ulmk_tid_t worker;
};

ulmk_ep_t g_dma_ep;
static struct dma_slot g_slots[DMA_MAX_SLOTS]
	__attribute__((section(".user_bss")));
static uint8_t g_dma_ready __attribute__((section(".user_bss")));

static const uint32_t g_lifcr_mask[DMA_MAX_SLOTS] = {
	0x0000003Du, 0x00000F40u, 0x003D0000u,
};
static const uint32_t g_tcif_mask[DMA_MAX_SLOTS] = {
	(1u << 5), (1u << 11), (1u << 21),
};
static const uint8_t g_irq_slot[DMA_MAX_SLOTS] = {
	ULMK_BOARD_IRQ_DMA_STR0,
	ULMK_BOARD_IRQ_DMA_STR1,
	ULMK_BOARD_IRQ_DMA_STR2,
};
static const uint8_t g_nvic[DMA_MAX_SLOTS] = {
	ULMK_BOARD_NVIC_DMA1_STR0,
	ULMK_BOARD_NVIC_DMA1_STR1,
	ULMK_BOARD_NVIC_DMA1_STR2,
};

static uint32_t flags_to_cr(uint32_t flags)
{
	uint32_t cr = DMA_CR_TCIE;

	if (flags & DMA_FLAG_MINC)
		cr |= DMA_CR_MINC;
	if (flags & DMA_FLAG_DIR_M2P)
		cr |= DMA_CR_DIR_M2P;
	if (flags & DMA_FLAG_PSIZE_16)
		cr |= DMA_CR_PSIZE_16;
	if (flags & DMA_FLAG_MSIZE_16)
		cr |= DMA_CR_MSIZE_16;
	return cr;
}

static void slot_arm_hw(DMA_TypeDef *dma, uint8_t slot)
{
	DMA_Stream_TypeDef *stream;
	DMAMUX_Channel_TypeDef *mux;
	struct dma_slot *s = &g_slots[slot];

	stream = (DMA_Stream_TypeDef *)((uintptr_t)dma + 0x10u +
					(uint32_t)slot * 0x18u);
	mux = (DMAMUX_Channel_TypeDef *)((uintptr_t)dma + 0x800u +
					 (uint32_t)slot * 4u);

	dma->LIFCR = g_lifcr_mask[slot];
	stream->CR = 0u;
	stream->PAR = (uint32_t)s->periph;
	stream->M0AR = (uint32_t)(uintptr_t)s->buf;
	if ((s->flags & DMA_FLAG_PSIZE_16) != 0u ||
	    (s->flags & DMA_FLAG_MSIZE_16) != 0u)
		stream->NDTR = s->len / 2u;
	else
		stream->NDTR = s->len;
	mux->CCR = s->mux_req;
	stream->CR = flags_to_cr(s->flags);
	ulmk_irq_ack(g_irq_slot[slot]);
	stream->CR |= DMA_CR_EN;
	__asm__ volatile("dsb" ::: "memory");
}

static int slot_wait_tc(DMA_TypeDef *dma, uint8_t slot, uint32_t timeout_ms)
{
	uint32_t elapsed = 0u;

	/*
	 * Poll TCIF with sleep (no busy-wait).  Short I2C xfers often complete
	 * before a separate WAIT is posted; relying only on the DMA IRQ races
	 * with NVIC mask/ack across the ARM→kick→WAIT window.
	 */
	for (;;) {
		if (dma->LISR & g_tcif_mask[slot]) {
			dma->LIFCR = g_lifcr_mask[slot];
			ulmk_irq_ack(g_irq_slot[slot]);
			return ULMK_OK;
		}
		if (timeout_ms != 0u && elapsed >= timeout_ms)
			return ULMK_ETIMEOUT;
		(void)ulmk_sleep_ms(1u);
		elapsed++;
	}
}

static void dma_worker(void *arg)
{
	uint8_t slot = (uint8_t)(uintptr_t)arg;
	struct dma_slot *s = &g_slots[slot];
	DMA_TypeDef *dma;
	uint32_t bits;
	uint32_t tmo;

	dma = (DMA_TypeDef *)ulmk_mem_map(
		(void *)(uintptr_t)ULMK_BOARD_DMA1_BASE,
		ULMK_BOARD_DMA1_FULL_MAP_SIZE,
		ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!dma)
		return;

	for (;;) {
		bits = 0u;
		if (ulmk_notif_wait(s->job_notif, 1u << DMA_NOTIF_JOB,
				    &bits) != ULMK_OK)
			continue;
		if (s->job == DMA_JOB_ARM) {
			tmo = s->timeout_ms ? s->timeout_ms : DMA_DEFAULT_WAIT_MS;
			/*
			 * M2P: CPU wrote slot buf → clean before DMA reads.
			 * P2M: invalidate before arm so DMA fills aren't
			 * shadowed by stale cache lines; re-inv after TC.
			 */
			if (s->flags & DMA_FLAG_DIR_M2P)
				board_dcache_clean(s->buf, s->len);
			else
				board_dcache_invalidate(s->buf, s->len);
			slot_arm_hw(dma, slot);
			/*
			 * Tell the client the stream is live so it can kick the
			 * peripheral, then block on TC here (same context as EN).
			 */
			ulmk_notif_signal(s->done_notif, 1u << DMA_NOTIF_ARMED);
			s->result = slot_wait_tc(dma, slot, tmo);
			if ((s->flags & DMA_FLAG_DIR_M2P) == 0u &&
			    s->result == ULMK_OK)
				board_dcache_invalidate(s->buf, s->len);
			ulmk_notif_signal(s->done_notif, 1u << DMA_NOTIF_DONE);
		} else {
			s->result = ULMK_EINVAL;
			ulmk_notif_signal(s->done_notif, 1u << DMA_NOTIF_DONE);
		}
	}
}

static int do_open(uint8_t slot, uint8_t mux_req, uint32_t flags)
{
	if (slot >= DMA_MAX_SLOTS)
		return ULMK_EINVAL;
	if (g_slots[slot].open)
		return ULMK_ENOSPC;
	g_slots[slot].mux_req = mux_req;
	g_slots[slot].flags = flags;
	g_slots[slot].open = 1u;
	return ULMK_OK;
}

static int do_close(uint8_t slot)
{
	if (slot >= DMA_MAX_SLOTS || !g_slots[slot].open)
		return ULMK_EINVAL;
	g_slots[slot].open = 0u;
	return ULMK_OK;
}

static int do_load(uint8_t slot, uint32_t offset, const uint8_t *data,
		   uint32_t len)
{
	uint32_t i;

	if (slot >= DMA_MAX_SLOTS || !g_slots[slot].open)
		return ULMK_EINVAL;
	if (!data || len == 0u || offset + len > DMA_XFER_MAX)
		return ULMK_EINVAL;
	for (i = 0u; i < len; i++)
		g_slots[slot].buf[offset + i] = data[i];
	return ULMK_OK;
}

static int do_arm(uint8_t slot, uintptr_t periph, uint32_t len,
		  uint32_t timeout_ms)
{
	struct dma_slot *s;
	uint32_t bits = 0u;
	int ret;

	if (slot >= DMA_MAX_SLOTS || !g_slots[slot].open)
		return ULMK_EINVAL;
	if (len == 0u || len > DMA_XFER_MAX)
		return ULMK_EINVAL;
	s = &g_slots[slot];
	s->periph = periph;
	s->len = len;
	s->timeout_ms = timeout_ms ? timeout_ms : DMA_DEFAULT_WAIT_MS;
	s->job = DMA_JOB_ARM;
	ulmk_notif_signal(s->job_notif, 1u << DMA_NOTIF_JOB);
	ret = ulmk_notif_wait(s->done_notif, 1u << DMA_NOTIF_ARMED, &bits);
	return ret;
}

static int do_wait(uint8_t slot, uint32_t timeout_ms, uint8_t *out,
		   uint32_t len)
{
	struct dma_slot *s;
	uint32_t bits = 0u;
	uint32_t i;
	int ret;

	if (slot >= DMA_MAX_SLOTS || !g_slots[slot].open)
		return ULMK_EINVAL;
	s = &g_slots[slot];
	(void)timeout_ms;
	ret = ulmk_notif_wait(s->done_notif, 1u << DMA_NOTIF_DONE, &bits);
	if (ret != ULMK_OK)
		return ret;
	if (out && len > 0u && (s->flags & DMA_FLAG_DIR_M2P) == 0u) {
		if (len > s->len)
			len = s->len;
		for (i = 0u; i < len; i++)
			out[i] = s->buf[i];
	}
	return s->result;
}

static void unpack_payload(const ulmk_msg_t *msg, uint32_t w0, uint8_t *dst,
			   uint32_t len)
{
	uint32_t i;

	for (i = 0u; i < len && i < DMA_XFER_MAX; i++)
		dst[i] = (uint8_t)((msg->words[w0 + (i / 4u)] >>
				    ((i % 4u) * 8u)) & 0xFFu);
}

static void pack_payload(ulmk_msg_t *msg, uint32_t w0, const uint8_t *src,
			 uint32_t len)
{
	uint32_t i;

	msg->words[w0] = 0u;
	msg->words[w0 + 1u] = 0u;
	msg->words[w0 + 2u] = 0u;
	msg->words[w0 + 3u] = 0u;
	for (i = 0u; i < len && i < DMA_XFER_MAX; i++)
		msg->words[w0 + (i / 4u)] |=
			(uint32_t)src[i] << ((i % 4u) * 8u);
}

static void dma_dispatcher(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t tmp[DMA_XFER_MAX];
	uint32_t len;

	(void)arg;
	for (;;) {
		if (ulmk_ep_recv(g_dma_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;
		reply.words[4] = 0u;
		reply.words[5] = 0u;

		if (msg.label == DMA_MSG_OPEN) {
			reply.words[0] = (uint32_t)do_open(
				(uint8_t)msg.words[0], (uint8_t)msg.words[1],
				msg.words[2]);
		} else if (msg.label == DMA_MSG_CLOSE) {
			reply.words[0] =
				(uint32_t)do_close((uint8_t)msg.words[0]);
		} else if (msg.label == DMA_MSG_LOAD) {
			len = msg.words[2];
			unpack_payload(&msg, 3u, tmp, len);
			reply.words[0] = (uint32_t)do_load(
				(uint8_t)msg.words[0], msg.words[1], tmp, len);
		} else if (msg.label == DMA_MSG_ARM) {
			/* words[3] optional timeout_ms for the inline TC wait */
			reply.words[0] = (uint32_t)do_arm(
				(uint8_t)msg.words[0],
				(uintptr_t)msg.words[1], msg.words[2],
				msg.words[3]);
		} else if (msg.label == DMA_MSG_WAIT) {
			len = msg.words[2];
			reply.words[0] = (uint32_t)do_wait(
				(uint8_t)msg.words[0], msg.words[1], tmp, len);
			if ((int)(int32_t)reply.words[0] == ULMK_OK &&
			    len > 0u)
				pack_payload(&reply, 2u, tmp, len);
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t dma_init(void)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t disp;
	ulmk_tid_t wtid;
	uint8_t i;
	static const char *const wnames[DMA_MAX_SLOTS] = {
		"dmaw0", "dmaw1", "dmaw2",
	};

	if (g_dma_ready)
		return (ulmk_tid_t)1u;

	g_dma_ep = ulmk_ep_create();
	if (g_dma_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	for (i = 0u; i < DMA_MAX_SLOTS; i++) {
		g_slots[i].irq_notif = ulmk_notif_create();
		g_slots[i].job_notif = ulmk_notif_create();
		g_slots[i].done_notif = ulmk_notif_create();
		if (g_slots[i].irq_notif == ULMK_NOTIF_INVALID ||
		    g_slots[i].job_notif == ULMK_NOTIF_INVALID ||
		    g_slots[i].done_notif == ULMK_NOTIF_INVALID)
			return ULMK_TID_INVALID;
		if (ulmk_irq_bind_hw(g_irq_slot[i], g_slots[i].irq_notif,
				     DMA_NOTIF_TC,
				     ULMK_BOARD_NVIC_SRC(g_nvic[i])) != ULMK_OK)
			return ULMK_TID_INVALID;
		if (ulmk_irq_enable(g_irq_slot[i]) != ULMK_OK)
			return ULMK_TID_INVALID;

		attr.name = wnames[i];
		attr.entry = dma_worker;
		attr.arg = (void *)(uintptr_t)i;
		attr.priority = 2u;
		attr.stack_size = DMA_STACK_SIZE;
		attr.privilege = ULMK_PRIV_DRIVER;
		wtid = ulmk_thread_create(&attr);
		if (wtid == ULMK_TID_INVALID)
			return ULMK_TID_INVALID;
		g_slots[i].worker = wtid;
		ulmk_cap_grant(wtid, ULMK_CAP_MAP_PERIPH);
		ulmk_cap_grant(wtid, ULMK_CAP_IRQ);
	}

	attr.name = "dma";
	attr.entry = dma_dispatcher;
	attr.arg = NULL;
	attr.priority = 2u;
	attr.stack_size = DMA_DISP_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	disp = ulmk_thread_create(&attr);
	if (disp == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(disp, ULMK_CAP_IRQ);
	g_dma_ready = 1u;
	return disp;
}
