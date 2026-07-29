/* SPDX-License-Identifier: MIT */
#ifndef DMA_INTERNAL_H
#define DMA_INTERNAL_H

#include <ulmk/microkernel.h>

/* One AHB-PDMA controller is wired for general use on this kit. */
#define DMA_MAX_INST	1u

enum {
	DMA_MSG_OPEN   = 1u,	/* words[0] = slot, [1] = mux, [2] = flags */
	DMA_MSG_MEMCPY = 2u,	/* words[0] = dst, [1] = src, [2] = len */
	DMA_MSG_RX_ARM = 3u,	/* words[0] = slot, [1] = dst, [2] = len */
	DMA_MSG_RX_WAIT = 4u,	/* words[0] = slot */
};

extern ulmk_ep_t g_dma_eps[DMA_MAX_INST];

#endif /* DMA_INTERNAL_H */
