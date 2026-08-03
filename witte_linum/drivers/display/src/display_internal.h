/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

#include <ulmk/microkernel.h>
#include <display.h>

/*
 * Legacy client labels above the DM opcode band (0x01..0x07).
 */
#define DISPLAY_MSG_WRITE	0x10u /* -> phys of back buffer */
#define DISPLAY_MSG_FLIP	0x11u
#define DISPLAY_MSG_ON		0x12u
#define DISPLAY_MSG_PRESENT	0x16u /* words[1]=phys */
#define DISPLAY_NOTIF_VSYNC	0u

extern ulmk_ep_t g_display_ep;

#endif /* DISPLAY_INTERNAL_H */
