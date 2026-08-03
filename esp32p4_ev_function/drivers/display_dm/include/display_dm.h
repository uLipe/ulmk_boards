/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_DM_H
#define DISPLAY_DM_H

#include <ulmk/microkernel.h>

/*
 * Device-manager adapter over the board display driver (DISPLAY_MSG_*).
 * Call display_init() first (or let display_dm_init do it).
 */
ulmk_tid_t display_dm_init(void);
ulmk_ep_t display_dm_ep(void);

#endif /* DISPLAY_DM_H */
