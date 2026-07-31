/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

#include <ulmk/microkernel.h>

/* One MIPI-DSI controller on this SoC. */
#define DISPLAY_MAX_INST	1u

enum {
	DISPLAY_MSG_WRITE = 1u,	/* -> back buffer pointer */
	DISPLAY_MSG_FLIP  = 2u,
	DISPLAY_MSG_ON    = 3u,	/* words[0] = on */
	DISPLAY_MSG_FB    = 4u,	/* words[0] = idx  -> pointer */
	DISPLAY_MSG_INFO  = 5u,	/* -> w, h, in_psram */
	DISPLAY_MSG_FB_NC = 6u,	/* words[0] = cached -> nc pointer */
	DISPLAY_MSG_PRESENT = 7u, /* words[0] = fb ptr; wait next DMA frame */
};

extern ulmk_ep_t g_display_eps[DISPLAY_MAX_INST];

#endif /* DISPLAY_INTERNAL_H */
