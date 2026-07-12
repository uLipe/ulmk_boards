/* SPDX-License-Identifier: MIT */
#ifndef CAN_H
#define CAN_H

#include <ulmk/microkernel.h>
#include <stdint.h>

#define CAN_MAX	1u

typedef struct {
	uint8_t tx_port;
	uint8_t tx_pin;
	uint8_t tx_alt;
	uint8_t rx_port;
	uint8_t rx_pin;
	uint8_t rx_alti;
} can_pins_t;

typedef struct {
	uint32_t id;
	uint8_t  dlc;
	uint8_t  data[8];
} can_frame_t;

ulmk_tid_t can_init(uint8_t n, const can_pins_t *pins, uint32_t bitrate);
int can_send(uint8_t n, const can_frame_t *frame);
int can_recv(uint8_t n, can_frame_t *frame);

#endif /* CAN_H */
