/* SPDX-License-Identifier: MIT */
/*
 * adc_dm — ulmk_dev_serve adapter over legacy adc_* client APIs.
 */
#include <stddef.h>
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_adc.h>
#include <adc.h>
#include "adc_dm.h"

#define ADC_DM_STACK	2048u

struct adc_dm_ctx {
	uint8_t mod;
	uint8_t opened;
	uint8_t ch;
};

static struct adc_dm_ctx g_ctx __attribute__((section(".user_bss")));
static ulmk_ep_t g_ep __attribute__((section(".user_bss")));
static ulmk_tid_t g_tid __attribute__((section(".user_bss")));

static int dm_open(void *arg)
{
	struct adc_dm_ctx *c = arg;

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

static int dm_read(void *arg, void *buf, size_t len, uint32_t flags)
{
	struct adc_dm_ctx *c = arg;
	uint16_t *out;
	int rc;

	(void)flags;
	if (!c || !buf || len < sizeof(*out))
		return ULMK_EINVAL;
	out = (uint16_t *)buf;

	rc = adc_read(c->ch, out);
	if (rc != ULMK_OK)
		return rc;
	return (int)sizeof(*out);
}

static int dm_ioctl(void *arg, uint32_t req, uint32_t *args, uint32_t nargs)
{
	struct adc_dm_ctx *c = arg;
	int rc;

	if (!c || !args || nargs < 1u)
		return ULMK_EINVAL;

	switch (req) {
	case ULMK_ADC_IOCTL_CONFIG:
		rc = adc_config((uint8_t)args[0]);
		if (rc != ULMK_OK)
			return rc;
		c->ch = (uint8_t)args[0];
		break;
	case ULMK_ADC_IOCTL_SELECT:
		c->ch = (uint8_t)args[0];
		break;
	default:
		return ULMK_ENOTSUP;
	}

	args[0] = (uint32_t)ULMK_OK;
	return ULMK_OK;
}

static const struct ulmk_dev_ops g_ops = {
	.open = dm_open,
	.close = dm_close,
	.read = dm_read,
	.write = NULL,
	.info = NULL,
	.ioctl = dm_ioctl,
	.submit = NULL,
	.wait = NULL,
};

static void adc_dm_server(void *arg)
{
	ulmk_dev_serve(g_ep, &g_ops, arg);
}

ulmk_ep_t adc_dm_ep(void)
{
	return g_ep;
}

ulmk_tid_t adc_dm_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = { 0 };
	ulmk_tid_t hw;

	if (g_ep != ULMK_EP_INVALID)
		return g_tid;

	/* DM before legacy — see pwm_dm_init (map-before-recv spin). */
	g_ep = ulmk_ep_create();
	if (g_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_ctx.mod = mod;
	g_ctx.opened = 0u;
	g_ctx.ch = 0u;

	attr.name = "adc_dm";
	attr.entry = adc_dm_server;
	attr.arg = &g_ctx;
	attr.priority = 2u;
	attr.stack_size = ADC_DM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	g_tid = ulmk_thread_create(&attr);
	if (g_tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(g_ep);
		g_ep = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}

	hw = adc_init(mod);
	if (hw == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	g_ctx.opened = 1u;
	return g_tid;
}
