/* SPDX-License-Identifier: MIT */
#ifndef TOUCH_INTERNAL_H
#define TOUCH_INTERNAL_H

#include <ulmk/microkernel.h>

/* One touch panel on this kit. */
#define TOUCH_MAX_INST	1u

/*
 * Legacy client label above the DM band so OPEN/CLOSE/READ stay clean.
 */
enum {
	TOUCH_MSG_READ = 0x10u,	/* -> x, y, pressed */
};

extern ulmk_ep_t g_touch_eps[TOUCH_MAX_INST];

#endif /* TOUCH_INTERNAL_H */
