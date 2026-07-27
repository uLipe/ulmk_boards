/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "board_config.h"
#include "pinmux_internal.h"

static int pinmux_call(uint8_t n, ulmk_msg_t *msg)
{
	if (n >= ULMK_BOARD_PINMUX_MAX || g_pinmux_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_pinmux_eps[n], msg);
}

int pinmux_config(uint8_t n, const pinmux_cfg_t *cfg)
{
	ulmk_msg_t msg;
	int rc;

	if (!cfg)
		return ULMK_EINVAL;
	msg.label    = PINMUX_MSG_CONFIG;
	msg.words[0] = ((uint32_t)cfg->port << 24) | ((uint32_t)cfg->pin << 16) |
		       ((uint32_t)cfg->dir << 8) | (uint32_t)cfg->pull;
	msg.words[1] = ((uint32_t)cfg->alt << 8) | (uint32_t)cfg->flags;
	msg.words[2] = 0u;
	msg.words[3] = 0u;
	rc = pinmux_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}
