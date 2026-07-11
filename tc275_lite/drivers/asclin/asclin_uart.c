/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/drivers/asclin/asclin_uart.c — bare ASCLIN UART (8N1, polled).
 */

#include <stdint.h>
#include "asclin_regs.h"

#define ASCLIN_POLL_LIMIT	500000u

static uint32_t tx_fill(void *base)
{
	return (ASCLIN_REG(base, ASCLIN_TXFIFOCON_OFF) & ASCLIN_TXFIFOCON_FILL_MASK)
		>> ASCLIN_TXFIFOCON_FILL_SHIFT;
}

static uint32_t rx_fill(void *base)
{
	return (ASCLIN_REG(base, ASCLIN_RXFIFOCON_OFF) & ASCLIN_RXFIFOCON_FILL_MASK)
		>> ASCLIN_RXFIFOCON_FILL_SHIFT;
}

static void set_baud(void *base, uint32_t baud, uint32_t fa_hz)
{
	static const uint32_t denom = 3072u;
	uint32_t numer;

	numer = (uint32_t)(((uint64_t)baud * denom * 16u + (fa_hz / 2u)) / fa_hz);
	if (numer == 0u)
		numer = 1u;
	if (numer > 0xFFFu)
		numer = 0xFFFu;
	ASCLIN_REG(base, ASCLIN_BRG_OFF) =
		(numer << ASCLIN_BRG_NUMER_SHIFT) |
		(denom << ASCLIN_BRG_DENOM_SHIFT);
}

void asclin_uart_init(void *base, uint8_t rx_alt, uint8_t tx_alt,
		      uint32_t baud, uint32_t fa_hz)
{
	uint32_t wait;
	uint32_t clc;

	/*
	 * CLC.DISR writes need CPU EndInit (see board_init asclin0 enable).
	 * Userspace must not touch CLC — only poll until the module is active.
	 */
	for (wait = 0u; wait < 1000u; wait++) {
		clc = ASCLIN_REG(base, ASCLIN_CLC_OFF);
		if (!(clc & ASCLIN_CLC_DISS))
			break;
	}

	ASCLIN_REG(base, ASCLIN_CSR_OFF) = ASCLIN_CSR_CLKSEL_NOCLK;
	/* IOCR.ALTI selects RX pin (0 = RXA = P14.1 on Lite Kit). */
	ASCLIN_REG(base, ASCLIN_IOCR_OFF) = (uint32_t)rx_alt & 0x7u;
	(void)tx_alt;

	ASCLIN_REG(base, ASCLIN_BITCON_OFF) =
		((uint32_t)ASCLIN_BITCON_OS_16 << 0) |
		((uint32_t)ASCLIN_BITCON_SP_DEFAULT << 8) |
		((uint32_t)ASCLIN_BITCON_PRESC_1 << 16);

	ASCLIN_REG(base, ASCLIN_FRAMECON_OFF) =
		ASCLIN_FRAMECON_STOP_1BIT | ASCLIN_FRAMECON_PAR_NONE |
		ASCLIN_FRAMECON_LSB_FIRST | ASCLIN_FRAMECON_MODE_INIT;
	ASCLIN_REG(base, ASCLIN_DATCON_OFF) = ASCLIN_DATCON_8BIT;
	set_baud(base, baud, fa_hz);

	ASCLIN_REG(base, ASCLIN_TXFIFOCON_OFF) = ASCLIN_TXFIFOCON_FLUSH;
	ASCLIN_REG(base, ASCLIN_TXFIFOCON_OFF) =
		ASCLIN_TXFIFOCON_ENO | ASCLIN_TXFIFOCON_INW_1;
	ASCLIN_REG(base, ASCLIN_RXFIFOCON_OFF) = ASCLIN_RXFIFOCON_FLUSH;
	ASCLIN_REG(base, ASCLIN_RXFIFOCON_OFF) =
		ASCLIN_RXFIFOCON_ENI | ASCLIN_RXFIFOCON_OUTW_1;

	/* MODE must be programmed with CSR.CLKSEL=0; then enable fCLC. */
	ASCLIN_REG(base, ASCLIN_FRAMECON_OFF) =
		(ASCLIN_REG(base, ASCLIN_FRAMECON_OFF) & ~(7u << 17)) |
		ASCLIN_FRAMECON_MODE_ASC;
	ASCLIN_REG(base, ASCLIN_CSR_OFF) = ASCLIN_CSR_CLKSEL_FA;
}

int asclin_uart_tx_byte(void *base, uint8_t byte)
{
	uint32_t i;

	for (i = 0u; i < ASCLIN_POLL_LIMIT; i++) {
		if (tx_fill(base) < ASCLIN_FIFO_DEPTH) {
			ASCLIN_REG(base, ASCLIN_TXDATA_OFF) = (uint32_t)byte;
			return 0;
		}
	}
	return -1;
}

int asclin_uart_rx_byte(void *base, uint8_t *out)
{
	uint32_t i;

	for (i = 0u; i < ASCLIN_POLL_LIMIT; i++) {
		if (rx_fill(base) > 0u) {
			*out = (uint8_t)(ASCLIN_REG(base, ASCLIN_RXDATA_OFF) & 0xFFu);
			return 0;
		}
	}
	return -1;
}
