/* SPDX-License-Identifier: MIT */
#ifndef PINMUX_INTERNAL_H
#define PINMUX_INTERNAL_H

#include <ulmk/microkernel.h>
#include <pinmux.h>
#include "IfxPort_reg.h"

#define PINMUX_MSG_CONFIG	1u

extern ulmk_ep_t g_pinmux_eps[];

/*
 * In-process helpers for other board drivers (same AS).
 * Call only after pinmux_init(); never mem_map PORTs yourself.
 */
Ifx_P *pinmux_port(uint8_t port);
int pinmux_apply(const pinmux_cfg_t *cfg);

#endif /* PINMUX_INTERNAL_H */
