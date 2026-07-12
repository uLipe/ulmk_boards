/* SPDX-License-Identifier: MIT */
#ifndef CAN_INTERNAL_H
#define CAN_INTERNAL_H

#include <ulmk/microkernel.h>
#include <can.h>

#define CAN_MSG_SEND	1u
#define CAN_MSG_RECV	2u

extern ulmk_ep_t g_can_eps[CAN_MAX];

#endif /* CAN_INTERNAL_H */
