/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/drivers/asclin/asclin_regs.h — TC27x ASCLIN register map.
 *
 * Offsets/bits match Infineon IfxAsclin_reg.h / IfxAsclin_regdef.h (TC27D).
 */

#ifndef ASCLIN_REGS_H
#define ASCLIN_REGS_H

#include <stdint.h>

#define ASCLIN_CLC_OFF		0x00u
#define ASCLIN_IOCR_OFF		0x04u
#define ASCLIN_ID_OFF		0x08u
#define ASCLIN_TXFIFOCON_OFF	0x0Cu
#define ASCLIN_RXFIFOCON_OFF	0x10u
#define ASCLIN_BITCON_OFF	0x14u
#define ASCLIN_FRAMECON_OFF	0x18u
#define ASCLIN_DATCON_OFF	0x1Cu
#define ASCLIN_BRG_OFF		0x20u
#define ASCLIN_TXDATA_OFF	0x44u
#define ASCLIN_RXDATA_OFF	0x48u
#define ASCLIN_CSR_OFF		0x4Cu

#define ASCLIN_REG(base, off) \
	(*((volatile uint32_t *)((uintptr_t)(base) + (uint32_t)(off))))

#define ASCLIN_CLC_DISS		(1u << 1)
#define ASCLIN_CSR_CLKSEL_NOCLK	0u
#define ASCLIN_CSR_CLKSEL_FA	1u
#define ASCLIN_BITCON_OS_16	15u
#define ASCLIN_BITCON_SP_DEFAULT 9u
#define ASCLIN_BITCON_PRESC_1	0u
#define ASCLIN_FRAMECON_STOP_1BIT (0u << 0)
#define ASCLIN_FRAMECON_PAR_NONE  (0u << 4)
#define ASCLIN_FRAMECON_LSB_FIRST (0u << 16)
#define ASCLIN_FRAMECON_MODE_INIT (0u << 17)
#define ASCLIN_FRAMECON_MODE_ASC  (1u << 17)
#define ASCLIN_DATCON_8BIT	7u

#define ASCLIN_TXFIFOCON_FLUSH	(1u << 0)
#define ASCLIN_TXFIFOCON_ENO	(1u << 1)
#define ASCLIN_TXFIFOCON_INW_1	(1u << 6)	/* 8-bit inlet */
#define ASCLIN_RXFIFOCON_FLUSH	(1u << 0)
#define ASCLIN_RXFIFOCON_ENI	(1u << 1)
#define ASCLIN_RXFIFOCON_OUTW_1	(1u << 6)	/* 8-bit outlet */

#define ASCLIN_TXFIFOCON_FILL_SHIFT 16u
#define ASCLIN_TXFIFOCON_FILL_MASK (0x1Fu << 16)
#define ASCLIN_RXFIFOCON_FILL_SHIFT 16u
#define ASCLIN_RXFIFOCON_FILL_MASK (0x1Fu << 16)
#define ASCLIN_FIFO_DEPTH	16u

#define ASCLIN_BRG_DENOM_SHIFT	0u
#define ASCLIN_BRG_NUMER_SHIFT	16u

#endif /* ASCLIN_REGS_H */
