/* SPDX-License-Identifier: MIT */
/*
 * drivers/port/port_regs.h — TC27x PORT register offsets (iLLD IfxPort).
 */
#ifndef PORT_REGS_H
#define PORT_REGS_H

#include <stdint.h>

#define PORT_OUT_OFF	0x00u
#define PORT_IOCR0_OFF	0x10u
#define PORT_IN_OFF	0x24u
#define PORT_OMR_OFF	0x04u

#define PORT_PC_OUT_PP	0x80u	/* push-pull general */
#define PORT_PC_IN_PU	0x10u	/* input pull-up */
#define PORT_PC_IN_FLOAT	0x00u

#define PORT_MAP_SIZE	0x100u

/* P00 .. P33 bases (sparse; only ports we use). */
#define PORT00_BASE	0xF003A000u
#define PORT02_BASE	0xF003A200u
#define PORT13_BASE	0xF003B200u
#define PORT14_BASE	0xF003B400u
#define PORT15_BASE	0xF003B500u
#define PORT20_BASE	0xF003C000u

static inline volatile uint32_t *port_reg(void *base, uint32_t off)
{
	return (volatile uint32_t *)((uintptr_t)base + off);
}

static inline void port_set_iocr(void *base, uint8_t pin, uint8_t pc)
{
	uint32_t iocr_off = PORT_IOCR0_OFF + ((uint32_t)(pin / 4u) * 4u);
	uint32_t shift = (uint32_t)(pin % 4u) * 8u;
	volatile uint32_t *iocr = port_reg(base, iocr_off);
	uint32_t v = *iocr;

	v &= ~(0xFFu << shift);
	v |= ((uint32_t)pc << shift);
	*iocr = v;
}

static inline void port_write_pin(void *base, uint8_t pin, int high)
{
	uint32_t mask = 1u << pin;

	if (high)
		*port_reg(base, PORT_OMR_OFF) = mask;		/* PS */
	else
		*port_reg(base, PORT_OMR_OFF) = mask << 16;	/* PCL */
}

static inline int port_read_pin(void *base, uint8_t pin)
{
	return (*port_reg(base, PORT_IN_OFF) >> pin) & 1u;
}

#endif /* PORT_REGS_H */
