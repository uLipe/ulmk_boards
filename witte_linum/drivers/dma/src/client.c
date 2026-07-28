/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "dma_internal.h"

static int dma_call(ulmk_msg_t *msg)
{
	int ret;

	if (g_dma_ep == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	ret = ulmk_ep_call(g_dma_ep, msg);
	if (ret != ULMK_OK)
		return ret;
	return (int)(int32_t)msg->words[0];
}

static void pack_bytes(ulmk_msg_t *msg, uint32_t w0, const uint8_t *data,
		       size_t len)
{
	size_t i;

	msg->words[w0] = 0u;
	msg->words[w0 + 1u] = 0u;
	msg->words[w0 + 2u] = 0u;
	if (!data)
		return;
	for (i = 0u; i < len && i < 12u; i++)
		msg->words[w0 + (i / 4u)] |=
			(uint32_t)data[i] << ((i % 4u) * 8u);
}

static void unpack_bytes(const ulmk_msg_t *msg, uint32_t w0, uint8_t *data,
			 size_t len)
{
	size_t i;

	if (!data)
		return;
	for (i = 0u; i < len && i < DMA_XFER_MAX; i++)
		data[i] = (uint8_t)((msg->words[w0 + (i / 4u)] >>
				     ((i % 4u) * 8u)) & 0xFFu);
}

static int dma_load(uint8_t slot, uint32_t offset, const uint8_t *data,
		    size_t len)
{
	ulmk_msg_t msg = {0};

	msg.label = DMA_MSG_LOAD;
	msg.words[0] = slot;
	msg.words[1] = offset;
	msg.words[2] = (uint32_t)len;
	pack_bytes(&msg, 3u, data, len);
	return dma_call(&msg);
}

int dma_channel_open(uint8_t stream_slot, uint8_t dmamux_req, uint32_t flags)
{
	ulmk_msg_t msg = {0};

	msg.label = DMA_MSG_OPEN;
	msg.words[0] = stream_slot;
	msg.words[1] = dmamux_req;
	msg.words[2] = flags;
	return dma_call(&msg);
}

int dma_channel_close(uint8_t stream_slot)
{
	ulmk_msg_t msg = {0};

	msg.label = DMA_MSG_CLOSE;
	msg.words[0] = stream_slot;
	return dma_call(&msg);
}

int dma_arm(uint8_t stream_slot, uintptr_t periph_addr, const void *buf,
	    size_t len)
{
	ulmk_msg_t msg = {0};
	const uint8_t *p = (const uint8_t *)buf;
	size_t off;
	size_t chunk;
	int ret;

	if (len == 0u || len > DMA_XFER_MAX)
		return ULMK_EINVAL;
	if (buf) {
		for (off = 0u; off < len; off += 12u) {
			chunk = len - off;
			if (chunk > 12u)
				chunk = 12u;
			ret = dma_load(stream_slot, (uint32_t)off, p + off,
				       chunk);
			if (ret != ULMK_OK)
				return ret;
		}
	}
	msg.label = DMA_MSG_ARM;
	msg.words[0] = stream_slot;
	msg.words[1] = (uint32_t)periph_addr;
	msg.words[2] = (uint32_t)len;
	msg.words[3] = 1000u; /* inline TC wait budget in worker */
	return dma_call(&msg);
}

int dma_wait(uint8_t stream_slot, void *out, size_t len, uint32_t timeout_ms)
{
	ulmk_msg_t msg = {0};
	int ret;

	msg.label = DMA_MSG_WAIT;
	msg.words[0] = stream_slot;
	msg.words[1] = timeout_ms;
	msg.words[2] = (uint32_t)len;
	ret = dma_call(&msg);
	if (ret == ULMK_OK && out && len > 0u)
		unpack_bytes(&msg, 2u, (uint8_t *)out, len);
	return ret;
}

int dma_xfer(uint8_t stream_slot, uintptr_t periph_addr, const void *buf,
	     size_t len, uint32_t timeout_ms)
{
	int ret;

	ret = dma_arm(stream_slot, periph_addr, buf, len);
	if (ret != ULMK_OK)
		return ret;
	return dma_wait(stream_slot, NULL, 0u, timeout_ms);
}

int dma_xfer_read(uint8_t stream_slot, uintptr_t periph_addr, void *buf,
		  size_t len, uint32_t timeout_ms)
{
	int ret;

	ret = dma_arm(stream_slot, periph_addr, NULL, len);
	if (ret != ULMK_OK)
		return ret;
	return dma_wait(stream_slot, buf, len, timeout_ms);
}
