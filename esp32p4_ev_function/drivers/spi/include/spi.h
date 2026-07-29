/* SPDX-License-Identifier: MIT */
#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include <ulmk/microkernel.h>

ulmk_tid_t spi_init(uint8_t n);

/*
 * Full-duplex transfer on GPSPI2 (not MSPI).
 * Inline IPC payload max 16 bytes (same pattern as board i2c).
 */
int spi_transfer(uint8_t n, const uint8_t *tx, uint8_t *rx, uint32_t len);

/* Soft loopback smoke: MOSI pad fed into MISO matrix. */
int spi_loopback(uint8_t n, const uint8_t *tx, uint8_t *rx, uint32_t len);

#endif /* SPI_H */
