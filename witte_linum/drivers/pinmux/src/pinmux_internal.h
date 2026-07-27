/* SPDX-License-Identifier: MIT */
#ifndef PINMUX_INTERNAL_H
#define PINMUX_INTERNAL_H

#include <ulmk/microkernel.h>
#include <pinmux.h>
#include "board_ll.h"

#define PINMUX_MSG_CONFIG	1u

extern ulmk_ep_t g_pinmux_eps[];

GPIO_TypeDef *pinmux_gpio(uint8_t port);
int pinmux_apply(const pinmux_cfg_t *cfg);

#endif /* PINMUX_INTERNAL_H */
