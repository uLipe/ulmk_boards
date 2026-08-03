/* SPDX-License-Identifier: MIT */
#ifndef TOUCH_DM_H
#define TOUCH_DM_H

#include <ulmk/microkernel.h>

/*
 * Device-manager adapter over the board touch driver (TOUCH_MSG_*).
 * Call touch_init() first (or let touch_dm_init do it).
 */
ulmk_tid_t touch_dm_init(void);
ulmk_ep_t touch_dm_ep(void);

#endif /* TOUCH_DM_H */
