/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include "pinmux_internal.h"
#include "board_config.h"

#ifndef ULMK_BOARD_PINMUX_MAX
#define ULMK_BOARD_PINMUX_MAX	1u
#endif

int pinmux_config(uint8_t n, const pinmux_cfg_t *cfg)
{
	ulmk_msg_t msg;
	int rc;

	if (!cfg || n >= ULMK_BOARD_PINMUX_MAX ||
	    g_pinmux_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;

	msg.label    = PINMUX_MSG_CONFIG;
	msg.words[0] = ((uint32_t)cfg->port << 8) | cfg->pin;
	msg.words[1] = cfg->dir;
	msg.words[2] = cfg->pull;
	msg.words[3] = cfg->alt | ((uint32_t)cfg->flags << 8);
	rc = ulmk_ep_call(g_pinmux_eps[n], &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}
