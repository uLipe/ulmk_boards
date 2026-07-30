/* SPDX-License-Identifier: MIT */
#ifndef GDMA_AXI_H
#define GDMA_AXI_H

#include <stdint.h>
#include <ulmk/microkernel.h>

/*
 * Peripheral trigger ids on the AXI-PDMA bus (SOC_GDMA_TRIG_PERIPH_*).
 * GPSPI2/3 live here — not on AHB-PDMA.
 */
#define GDMA_AXI_PERIPH_NONE	0x3Fu
#define GDMA_AXI_PERIPH_SPI2	1u
#define GDMA_AXI_PERIPH_SPI3	2u

/*
 * CH0 is reserved for mem-to-mem.  Each peripheral instance owns one
 * RX/TX pair, so GPSPI2 and GPSPI3 never reprogram each other's channel.
 */
#define GDMA_AXI_SLOT_MEM	0u
#define GDMA_AXI_SLOT_RX(ch)	(1u + (((ch) - 1u) * 2u))
#define GDMA_AXI_SLOT_TX(ch)	(2u + (((ch) - 1u) * 2u))

ulmk_tid_t gdma_axi_init(uint8_t n);

int gdma_axi_channel_open(uint8_t slot, uint32_t mux, uint32_t flags);
int gdma_axi_memcpy(void *dst, const void *src, uint32_t len);

int gdma_axi_rx_arm(uint8_t slot, void *dst, uint32_t len);
int gdma_axi_rx_wait(uint8_t slot);
int gdma_axi_tx_arm(uint8_t slot, const void *src, uint32_t len);
int gdma_axi_tx_wait(uint8_t slot);

#endif /* GDMA_AXI_H */
