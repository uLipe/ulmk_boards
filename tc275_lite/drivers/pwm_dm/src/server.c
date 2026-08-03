/* SPDX-License-Identifier: MIT */
/*
 * pwm_dm — ulmk_dev_serve adapter over legacy pwm_* client APIs.
 */
#include <stddef.h>
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_pwm.h>
#include <pwm.h>
#include "pwm_dm.h"

#define PWM_DM_STACK	2048u

struct pwm_dm_ctx {
	uint8_t mod;
	uint8_t opened;
};

static struct pwm_dm_ctx g_ctx __attribute__((section(".user_bss")));
static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static ulmk_tid_t g_tid __attribute__((section(".user_bss")));

static int dm_open(void *arg)
{
	struct pwm_dm_ctx *c = arg;

	if (!c)
		return ULMK_EINVAL;
	if (!c->opened)
		return ULMK_EINVAL;
	return ULMK_OK;
}

static int dm_close(void *arg)
{
	(void)arg;
	return ULMK_OK;
}

static int dm_ioctl(void *arg, uint32_t req, uint32_t *args, uint32_t nargs)
{
	int rc;

	(void)arg;
	if (!args || nargs < 1u)
		return ULMK_EINVAL;

	switch (req) {
	case ULMK_PWM_IOCTL_CONFIG:
		if (nargs < 3u)
			return ULMK_EINVAL;
		rc = pwm_config((uint8_t)args[0], args[1], args[2]);
		break;
	case ULMK_PWM_IOCTL_SET_DUTY:
		if (nargs < 2u)
			return ULMK_EINVAL;
		rc = pwm_set_duty((uint8_t)args[0], args[1]);
		break;
	case ULMK_PWM_IOCTL_ENABLE:
		if (nargs < 2u)
			return ULMK_EINVAL;
		rc = pwm_enable((uint8_t)args[0], (int)args[1]);
		break;
	default:
		return ULMK_ENOTSUP;
	}

	if (rc != ULMK_OK)
		return rc;
	args[0] = (uint32_t)ULMK_OK;
	return ULMK_OK;
}

static const struct ulmk_dev_ops g_ops = {
	.open = dm_open,
	.close = dm_close,
	.read = NULL,
	.write = NULL,
	.info = NULL,
	.ioctl = dm_ioctl,
	.submit = NULL,
	.wait = NULL,
};

static void pwm_dm_server(void *arg)
{
	ulmk_dev_serve(g_ep, &g_ops, arg);
}

ulmk_ep_t pwm_dm_ep(void)
{
	return g_ep;
}

ulmk_tid_t pwm_dm_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = { 0 };

	if (g_ep != ULMK_EP_INVALID)
		return g_tid;

	/*
	 * Adapter state must live in .user_bss — plain BSS can land in kernel
	 * SRAM and the PRIV_DRIVER serve thread then traps on g_ep before recv.
	 *
	 * Do not call pwm_init() here: creating the GTM server before the DM
	 * thread has entered recv races on TriCore and yields a silent hang
	 * before the first console line.  Root calls pwm_dm_bind_hw() after
	 * ulmk_open().
	 */
	g_ep = ulmk_ep_create();
	if (g_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_ctx.mod = mod;
	g_ctx.opened = 1u;

	attr.name = "pwm_dm";
	attr.entry = pwm_dm_server;
	attr.arg = &g_ctx;
	attr.priority = 2u;
	attr.stack_size = PWM_DM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	g_tid = ulmk_thread_create(&attr);
	if (g_tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(g_ep);
		g_ep = ULMK_EP_INVALID;
		g_ctx.opened = 0u;
		return ULMK_TID_INVALID;
	}
	return g_tid;
}

ulmk_tid_t pwm_dm_bind_hw(void)
{
	return pwm_init(g_ctx.mod);
}
