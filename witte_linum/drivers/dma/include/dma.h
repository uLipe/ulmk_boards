/* SPDX-License-Identifier: MIT */
#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include "board_config.h"

#define DMA_MAX_SLOTS	ULMK_BOARD_DMA_MAX_SLOTS
#define DMA_XFER_MAX	16u

/* Channel flags stored at open; OR'd into stream CR (minus EN). */
#define DMA_FLAG_DIR_M2P	(1u << 0)	/* memory → peripheral */
#define DMA_FLAG_MINC		(1u << 1)
#define DMA_FLAG_PSIZE_16	(1u << 2)
#define DMA_FLAG_MSIZE_16	(1u << 3)

ulmk_tid_t dma_init(void);
int dma_channel_open(uint8_t stream_slot, uint8_t dmamux_req, uint32_t flags);
int dma_channel_close(uint8_t stream_slot);

/*
 * Arm stream (copy-in for M2P) and enable. Caller then kicks the peripheral
 * and calls dma_wait().
 */
int dma_arm(uint8_t stream_slot, uintptr_t periph_addr,
	    const void *buf, size_t len);

/* Wait TC; copy-out for P2M when @p out is non-NULL. */
int dma_wait(uint8_t stream_slot, void *out, size_t len, uint32_t timeout_ms);

/* Convenience: dma_arm + dma_wait (no peripheral kick in between). */
int dma_xfer(uint8_t stream_slot, uintptr_t periph_addr,
	     const void *buf, size_t len, uint32_t timeout_ms);
int dma_xfer_read(uint8_t stream_slot, uintptr_t periph_addr,
		  void *buf, size_t len, uint32_t timeout_ms);

#endif /* DMA_H */
