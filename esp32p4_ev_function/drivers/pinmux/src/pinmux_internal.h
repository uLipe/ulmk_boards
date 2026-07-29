/* SPDX-License-Identifier: MIT */
#ifndef PINMUX_INTERNAL_H
#define PINMUX_INTERNAL_H

#include <ulmk/microkernel.h>
#include <pinmux.h>

#define PINMUX_MSG_CONFIG	1u

/* SIG_GPIO_OUT — pad driven from GPIO_OUT when ALT_GPIO. */
#define PINMUX_SIG_GPIO_OUT	0x100u

extern ulmk_ep_t g_pinmux_eps[];

/*
 * In-process helpers for other board drivers (same AS).
 * Call only after pinmux_init(); do not touch IOMUX/GPIO matrix yourself.
 */
int pinmux_apply(const pinmux_cfg_t *cfg);

#endif /* PINMUX_INTERNAL_H */
