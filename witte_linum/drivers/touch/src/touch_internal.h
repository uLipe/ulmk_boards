/* SPDX-License-Identifier: MIT */
#ifndef TOUCH_INTERNAL_H
#define TOUCH_INTERNAL_H

#include <ulmk/microkernel.h>
#include <touch.h>

/*
 * Legacy client labels above the DM opcode band.
 */
#define TOUCH_MSG_WAIT		0x10u /* words[0]=timeout_ms */
#define TOUCH_MSG_READ_XY	0x11u /* -> x, y */
#define TOUCH_MSG_POLL		0x12u /* non-blocking; returns 1/0 + x,y */
#define TOUCH_NOTIF_EXTI	0u
#define FT5X06_REG_TD_STATUS	0x02u

extern ulmk_ep_t g_touch_ep;

#endif /* TOUCH_INTERNAL_H */
