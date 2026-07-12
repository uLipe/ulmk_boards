/* SPDX-License-Identifier: MIT */
#ifndef CAN_H
#define CAN_H

#include <ulmk/microkernel.h>
#include <stdint.h>

#define CAN_MAX	1u

/*
 * Lite Kit TLE9251 (node 0): TX P20.8 alt5, RX P20.7 (RXSEL=b),
 * transceiver standby #NEN = P20.6 (drive low for normal mode).
 */
typedef struct {
	uint8_t tx_port;
	uint8_t tx_pin;
	uint8_t tx_alt;
	uint8_t rx_port;
	uint8_t rx_pin;
	uint8_t rx_alti;	/* RXSEL: 0=a … 1=b (Lite Kit) */
	uint8_t nen_port;	/* 0xFF = no #NEN GPIO */
	uint8_t nen_pin;
	uint8_t loopback;	/* 1 = MultiCAN internal LBM */
} can_cfg_t;

typedef struct {
	uint32_t id;
	uint8_t  dlc;
	uint8_t  data[8];
} can_frame_t;

ulmk_tid_t can_init(uint8_t n, const can_cfg_t *cfg, uint32_t bitrate);
int can_send(uint8_t n, const can_frame_t *frame);
int can_recv(uint8_t n, can_frame_t *frame);
uint32_t can_last_diag(void);

#endif /* CAN_H */
