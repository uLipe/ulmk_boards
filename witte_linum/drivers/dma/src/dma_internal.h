/* SPDX-License-Identifier: MIT */
#ifndef DMA_INTERNAL_H
#define DMA_INTERNAL_H

#include <ulmk/microkernel.h>
#include <dma.h>

#define DMA_MSG_OPEN		1u
#define DMA_MSG_CLOSE		2u
#define DMA_MSG_ARM		3u
#define DMA_MSG_WAIT		4u
#define DMA_MSG_LOAD		5u

#define DMA_JOB_ARM		1u
#define DMA_JOB_WAIT		2u

#define DMA_NOTIF_TC		0u
#define DMA_NOTIF_JOB		0u
#define DMA_NOTIF_ARMED		0u
#define DMA_NOTIF_DONE		1u

#define DMA_CR_EN		(1u << 0)
#define DMA_CR_TCIE		(1u << 4)
#define DMA_CR_MINC		(1u << 10)
#define DMA_CR_DIR_M2P		(1u << 6)
#define DMA_CR_PSIZE_16		(1u << 11)
#define DMA_CR_MSIZE_16		(1u << 13)

extern ulmk_ep_t g_dma_ep;

#endif /* DMA_INTERNAL_H */
