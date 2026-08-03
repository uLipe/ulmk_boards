/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

#include <ulmk/microkernel.h>

/* One MIPI-DSI controller on this SoC. */
#define DISPLAY_MAX_INST	1u

/*
 * Legacy client labels live above the DM opcode band (0x01..0x07) so
 * ulmk_open/close/read/write/info never collide with display_flip/on/fb.
 */
enum {
	DISPLAY_MSG_WRITE   = 0x10u, /* -> back buffer ptr */
	DISPLAY_MSG_FLIP    = 0x11u,
	DISPLAY_MSG_ON      = 0x12u,
	DISPLAY_MSG_FB      = 0x13u, /* words[0]=idx -> ptr */
	DISPLAY_MSG_INFO    = 0x14u, /* legacy w,h,psram */
	DISPLAY_MSG_FB_NC   = 0x15u,
	DISPLAY_MSG_PRESENT = 0x16u, /* words[0]=fb ptr */
};

extern ulmk_ep_t g_display_eps[DISPLAY_MAX_INST];

#endif /* DISPLAY_INTERNAL_H */
