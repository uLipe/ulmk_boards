/* SPDX-License-Identifier: MIT */
#ifndef DMA_H
#define DMA_H
#include <stdint.h>
#include <ulmk/microkernel.h>

/* Peripheral trigger ids, as wired to the AHB bus of the controller. */
#define DMA_PERIPH_NONE		0x3Fu
#define DMA_PERIPH_ADC0		8u

/* Slot 0 is the memory-to-memory channel; slot 1 receives from a peripheral. */
#define DMA_SLOT_MEM		0u
#define DMA_SLOT_PERI		1u

ulmk_tid_t dma_init(uint8_t n);

/*
 * Bind @slot to @mux (DMA_PERIPH_*).  DMA_PERIPH_NONE leaves the slot in
 * memory-to-memory mode.  @flags is reserved.
 */
int dma_channel_open(uint8_t slot, uint32_t mux, uint32_t flags);
int dma_memcpy(void *dst, const void *src, uint32_t len);

/*
 * Receive one block from the peripheral bound to @slot.  Arming is separate
 * from waiting because the source has to be started only once the channel is
 * already listening, otherwise the first samples are lost.  dma_rx_wait()
 * returns the byte count the controller stored, or a negative error.
 */
int dma_rx_arm(uint8_t slot, void *dst, uint32_t len);
int dma_rx_wait(uint8_t slot);
#endif
