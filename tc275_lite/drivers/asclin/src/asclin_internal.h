/* SPDX-License-Identifier: MIT */
#ifndef ASCLIN_INTERNAL_H
#define ASCLIN_INTERNAL_H

#include <ulmk/microkernel.h>
#include <asclin.h>

#define ASCLIN_MSG_TX_BYTE	1u
#define ASCLIN_MSG_RX_BYTE	2u
#define ASCLIN_MSG_RX_BYTE_NB	3u
#define ASCLIN_MSG_SET_BAUD	4u

extern ulmk_ep_t g_asclin_eps[ASCLIN_MAX];

#endif /* ASCLIN_INTERNAL_H */
