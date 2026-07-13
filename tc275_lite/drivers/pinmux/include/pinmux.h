/* SPDX-License-Identifier: MIT */
#ifndef PINMUX_H
#define PINMUX_H

#include <ulmk/microkernel.h>
#include <stdint.h>

#define PINMUX_DIR_IN		0u
#define PINMUX_DIR_OUT		1u

#define PINMUX_PULL_NONE	0u
#define PINMUX_PULL_UP		1u
#define PINMUX_PULL_DOWN	2u

/* 0 = GPIO pad; 1..7 = Port alternate output. */
#define PINMUX_ALT_GPIO		0u

#define PINMUX_F_OPENDRAIN	(1u << 0)

typedef struct {
	uint8_t port;
	uint8_t pin;
	uint8_t dir;
	uint8_t pull;
	uint8_t alt;
	uint8_t flags;
} pinmux_cfg_t;

/*
 * pinmux_init — map board PORT modules once and start the pinmux server.
 * @p n is the pinmux controller instance (Lite Kit: 0 only).
 */
ulmk_tid_t pinmux_init(uint8_t n);

/* Apply pad mode (IPC). Apps may call this; peripheral drivers also do. */
int pinmux_config(uint8_t n, const pinmux_cfg_t *cfg);

#endif /* PINMUX_H */
