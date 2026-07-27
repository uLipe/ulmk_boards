/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

#include <ulmk/microkernel.h>
#include <display.h>

#define DISPLAY_MSG_WRITE	1u
#define DISPLAY_MSG_FLIP	2u
#define DISPLAY_MSG_ON		3u
#define DISPLAY_NOTIF_VSYNC	0u

extern ulmk_ep_t g_display_ep;

#endif /* DISPLAY_INTERNAL_H */
