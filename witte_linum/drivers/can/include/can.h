/* SPDX-License-Identifier: MIT */
#ifndef CAN_H
#define CAN_H

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"

#define CAN_MAX	ULMK_BOARD_CAN_MAX
#define CAN_FLAG_LOOPBACK	(1u << 0)

typedef struct {
	uint32_t id;
	uint8_t dlc;
	uint8_t data[8];
} can_frame_t;

ulmk_tid_t can_init(uint8_t n, uint32_t bitrate, uint32_t flags);
int can_send(uint8_t n, const can_frame_t *frame);
int can_recv(uint8_t n, can_frame_t *frame);

#endif /* CAN_H */
