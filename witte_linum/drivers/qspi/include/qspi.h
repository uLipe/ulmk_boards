/* SPDX-License-Identifier: MIT */
#ifndef QSPI_H
#define QSPI_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "board_config.h"

#define QSPI_MAX	ULMK_BOARD_QSPI_MAX

ulmk_tid_t qspi_init(uint8_t n);
int qspi_cmd_read(uint8_t n, uint8_t cmd, uint8_t *data, size_t len);
int qspi_cmd_read_dbg(uint8_t n, uint8_t cmd, uint8_t *data, size_t len,
		      uint32_t *sr_out);
uint32_t qspi_last_cr(void);
int qspi_read(uint8_t n, uint32_t addr, uint8_t *data, size_t len);

#endif /* QSPI_H */
