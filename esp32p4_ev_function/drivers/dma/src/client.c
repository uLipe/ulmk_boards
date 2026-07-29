/* SPDX-License-Identifier: MIT */
/*
 * Client side of the AHB-PDMA driver: marshals calls to the server thread,
 * which is the only context that touches the controller.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "dma.h"
#include "dma_internal.h"

static int dma_call(uint32_t label, const uint32_t *words, uint32_t count)
{
	ulmk_msg_t msg;
	uint32_t i;

	if (g_dma_eps[0] == ULMK_EP_INVALID)
		return ULMK_EINVAL;

	msg.label = label;
	for (i = 0u; i < count; i++)
		msg.words[i] = words[i];
	if (ulmk_ep_call(g_dma_eps[0], &msg) != ULMK_OK)
		return ULMK_EINVAL;
	return (int)msg.words[0];
}

int dma_channel_open(uint8_t slot, uint32_t mux, uint32_t flags)
{
	uint32_t w[3];

	w[0] = slot;
	w[1] = mux;
	w[2] = flags;
	return dma_call(DMA_MSG_OPEN, w, 3u);
}

int dma_memcpy(void *dst, const void *src, uint32_t len)
{
	uint32_t w[3];

	w[0] = (uint32_t)(uintptr_t)dst;
	w[1] = (uint32_t)(uintptr_t)src;
	w[2] = len;
	return dma_call(DMA_MSG_MEMCPY, w, 3u);
}

int dma_rx_arm(uint8_t slot, void *dst, uint32_t len)
{
	uint32_t w[3];

	w[0] = slot;
	w[1] = (uint32_t)(uintptr_t)dst;
	w[2] = len;
	return dma_call(DMA_MSG_RX_ARM, w, 3u);
}

int dma_rx_wait(uint8_t slot)
{
	uint32_t w[1];

	w[0] = slot;
	return dma_call(DMA_MSG_RX_WAIT, w, 1u);
}
