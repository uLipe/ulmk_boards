/* SPDX-License-Identifier: MIT */
#ifndef CAN_H
#define CAN_H

#include <ulmk/microkernel.h>
#include <stdint.h>
#include "board_config.h"

#define CAN_MAX	ULMK_BOARD_CAN_MAX

typedef struct {
	uint32_t id;
	uint8_t  dlc;
	uint8_t  data[8];
} can_frame_t;

/*
 * @p n is the MultiCAN node / logical instance.
 * Pins + #NEN come from board_config; @p loopback enables internal LBM.
 */
ulmk_tid_t can_init(uint8_t n, uint32_t bitrate, int loopback);
int can_send(uint8_t n, const can_frame_t *frame);
int can_recv(uint8_t n, can_frame_t *frame);
uint32_t can_last_diag(void);

#endif /* CAN_H */
