/* SPDX-License-Identifier: MIT */
#ifndef TOUCH_INTERNAL_H
#define TOUCH_INTERNAL_H

#include <ulmk/microkernel.h>
#include <touch.h>

#define TOUCH_MSG_WAIT		1u
#define TOUCH_MSG_READ_XY	2u
#define TOUCH_MSG_POLL		3u
#define TOUCH_NOTIF_EXTI	0u
#define FT5X06_REG_TD_STATUS	0x02u

extern ulmk_ep_t g_touch_ep;

#endif /* TOUCH_INTERNAL_H */
