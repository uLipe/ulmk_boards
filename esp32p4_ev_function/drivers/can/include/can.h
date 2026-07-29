/* SPDX-License-Identifier: MIT */
#ifndef CAN_H
#define CAN_H
#include <stdint.h>
#include <ulmk/microkernel.h>
ulmk_tid_t can_init(uint8_t n);
int can_send(uint32_t id, const uint8_t *data, uint8_t len);
int can_recv(uint32_t *id, uint8_t *data, uint8_t *len);
#endif
