/* SPDX-License-Identifier: MIT */
#ifndef CAN_INTERNAL_H
#define CAN_INTERNAL_H

#include <ulmk/microkernel.h>

/* Only TWAI0 is wired on this kit. */
#define CAN_MAX_INST	1u

enum {
	CAN_MSG_SEND = 1u,	/* words[0] = id, [1..2] = payload, [3] = len */
	CAN_MSG_RECV = 2u,	/* -> id, payload, len */
};

extern ulmk_ep_t g_can_eps[CAN_MAX_INST];

#endif /* CAN_INTERNAL_H */
