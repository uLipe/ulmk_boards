/* SPDX-License-Identifier: MIT */
/*
 * gpio_dm — ulmk_dev_serve adapter over legacy gpio_* client APIs.
 */
#include <stddef.h>
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_gpio.h>
#include <gpio.h>
#include "gpio_dm.h"
#include "board_config.h"

#define GPIO_DM_STACK	2048u

struct gpio_dm_ctx {
	uint8_t n;
	uint8_t opened;
};

static struct gpio_dm_ctx g_ctx[GPIO_MAX] __attribute__((section(".user_bss")));
static ulmk_ep_t g_eps[GPIO_MAX] __attribute__((section(".user_bss")));
static ulmk_tid_t g_tids[GPIO_MAX] __attribute__((section(".user_bss")));

static int dm_open(void *arg)
{
	struct gpio_dm_ctx *c = arg;

	if (!c || c->n >= GPIO_MAX)
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

static int dm_write(void *arg, const void *buf, size_t len, uint32_t flags)
{
	struct gpio_dm_ctx *c = arg;
	const struct ulmk_gpio_pin_val *pv;

	(void)flags;
	if (!c || !buf || len < sizeof(*pv))
		return ULMK_EINVAL;
	pv = (const struct ulmk_gpio_pin_val *)buf;
	return gpio_set(c->n, pv->pin, (int)pv->value);
}

static int dm_ioctl(void *arg, uint32_t req, uint32_t *args, uint32_t nargs)
{
	struct gpio_dm_ctx *c = arg;
	int value;
	int rc;

	if (!c || !args)
		return ULMK_EINVAL;

	switch (req) {
	case ULMK_GPIO_IOCTL_CONFIG:
		if (nargs < 3u)
			return ULMK_EINVAL;
		rc = gpio_config(c->n, (uint16_t)args[0], args[1], args[2]);
		if (rc != ULMK_OK)
			return rc;
		args[0] = (uint32_t)ULMK_OK;
		return ULMK_OK;

	case ULMK_GPIO_IOCTL_GET:
		if (nargs < 2u)
			return ULMK_EINVAL;
		rc = gpio_get(c->n, (uint16_t)args[0], &value);
		if (rc != ULMK_OK)
			return rc;
		args[0] = (uint32_t)ULMK_OK;
		args[1] = (uint32_t)(int32_t)value;
		return ULMK_OK;

	default:
		return ULMK_ENOTSUP;
	}
}

static const struct ulmk_dev_ops g_ops = {
	.open = dm_open,
	.close = dm_close,
	.read = NULL,
	.write = dm_write,
	.info = NULL,
	.ioctl = dm_ioctl,
	.submit = NULL,
	.wait = NULL,
};

static void gpio_dm_server(void *arg)
{
	struct gpio_dm_ctx *c = arg;

	ulmk_dev_serve(g_eps[c->n], &g_ops, c);
}

ulmk_ep_t gpio_dm_ep(uint8_t n)
{
	if (n >= GPIO_MAX)
		return ULMK_EP_INVALID;
	return g_eps[n];
}

ulmk_tid_t gpio_dm_init(uint8_t n)
{
	ulmk_thread_attr_t attr = { 0 };
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	ulmk_tid_t hw;

	if (n >= GPIO_MAX)
		return ULMK_TID_INVALID;
	if (g_eps[n] != ULMK_EP_INVALID)
		return g_tids[n];

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_ctx[n].n = n;
	g_ctx[n].opened = 1u;
	g_eps[n] = ep;

	attr.name = "gpio_dm";
	attr.entry = gpio_dm_server;
	attr.arg = &g_ctx[n];
	attr.priority = 2u;
	attr.stack_size = GPIO_DM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		g_eps[n] = ULMK_EP_INVALID;
		g_ctx[n].opened = 0u;
		return ULMK_TID_INVALID;
	}
	g_tids[n] = tid;

	/*
	 * board_services_init() may already own gpio — INVALID is OK.
	 */
	hw = gpio_init(n);
	(void)hw;
	return tid;
}
