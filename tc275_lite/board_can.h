/* SPDX-License-Identifier: MIT */
#ifndef BOARD_CAN_H
#define BOARD_CAN_H

#include <ulmk/microkernel.h>
#include <stdint.h>

ulmk_tid_t board_can_start(const ulmk_boot_info_t *info);
int board_can_config(uint32_t bitrate);
int board_can_send(uint32_t id, const uint8_t *data, uint8_t len);
int board_can_recv(uint32_t *id, uint8_t *data, uint8_t *len);
int board_can_subscribe(ulmk_notif_t n, uint32_t bit);

#endif /* BOARD_CAN_H */
