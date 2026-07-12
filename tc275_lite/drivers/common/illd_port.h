/* SPDX-License-Identifier: MIT */
/*
 * drivers/common/illd_port.h — resolve MODULE_Pxx + MPU map helper.
 */
#ifndef ILLD_PORT_H
#define ILLD_PORT_H

#include <stdint.h>
#include <stddef.h>
#include "IfxPort_reg.h"
#include "Port/Std/IfxPort.h"

#define ILLD_PORT_MAP_SIZE	0x100u

static inline Ifx_P *illd_port_module(uint8_t port)
{
	switch (port) {
	case 0u:  return &MODULE_P00;
	case 2u:  return &MODULE_P02;
	case 10u: return &MODULE_P10;
	case 11u: return &MODULE_P11;
	case 12u: return &MODULE_P12;
	case 13u: return &MODULE_P13;
	case 14u: return &MODULE_P14;
	case 15u: return &MODULE_P15;
	case 20u: return &MODULE_P20;
	case 21u: return &MODULE_P21;
	case 22u: return &MODULE_P22;
	case 23u: return &MODULE_P23;
	case 32u: return &MODULE_P32;
	case 33u: return &MODULE_P33;
	case 34u: return &MODULE_P34;
	default:  return NULL;
	}
}

/*
 * IfxPort_setPinMode() lives in IfxPort.c and pulls EndInit/mfcr paths.
 * For digital ports the IOCR write needs no EndInit — mirror that path here
 * using the iLLD SFR type.
 */
static inline void illd_port_set_mode(Ifx_P *port, uint8_t pin,
				      IfxPort_Mode mode)
{
	volatile Ifx_P_IOCR0 *iocr = &port->IOCR0;
	uint8_t iocr_index = (uint8_t)(pin / 4u);
	uint8_t shift = (uint8_t)((pin & 3u) * 8u);
	uint32_t mask = 0xFFu << shift;
	uint32_t val = ((uint32_t)mode) << shift;

	iocr[iocr_index].U = (iocr[iocr_index].U & ~mask) | val;
}

#endif /* ILLD_PORT_H */
