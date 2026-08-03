/* SPDX-License-Identifier: MIT */
#ifndef CAN_DM_H
#define CAN_DM_H

#include <ulmk/microkernel.h>
#include <stdint.h>

/*
 * Device-manager adapter over the legacy MultiCAN client (can.h).
 * Registers as /dev/canN via board_devices_register_can() or manually.
 *
 * open() calls can_init() with the bitrate/loopback stored at dm_init time.
 */
ulmk_tid_t can_dm_init(uint8_t n, uint32_t bitrate, int loopback);
ulmk_ep_t can_dm_ep(uint8_t n);

#endif /* CAN_DM_H */
