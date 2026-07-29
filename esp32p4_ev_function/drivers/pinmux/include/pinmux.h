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

/* 0 = GPIO pad (SIG_GPIO_OUT); 1 = GPIO matrix peripheral signal. */
#define PINMUX_ALT_GPIO		0u
#define PINMUX_ALT_MATRIX	1u

#define PINMUX_F_OPENDRAIN	(1u << 0)
/* Keep input enable even for outputs (pad readback). */
#define PINMUX_F_IE		(1u << 1)
/* Route peripheral OE (oen_sel=0); default uses GPIO_ENABLE. */
#define PINMUX_F_PERIPH_OE	(1u << 2)

/*
 * ESP32-P4 flat GPIO numbering: port unused (0), pin = GPIO 0..56.
 * matrix_out / matrix_in: GPIO matrix signal indices (when ALT_MATRIX).
 * matrix_in == 0 means do not program FUNC_IN_SEL.
 */
typedef struct {
	uint8_t  port;
	uint8_t  pin;
	uint8_t  dir;
	uint8_t  pull;
	uint8_t  alt;
	uint8_t  flags;
	uint16_t matrix_out;
	uint16_t matrix_in;
} pinmux_cfg_t;

ulmk_tid_t pinmux_init(uint8_t n);
int pinmux_config(uint8_t n, const pinmux_cfg_t *cfg);

#endif /* PINMUX_H */
