/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_input.h>
#include <touch.h>
#include <touch_dm.h>

static ulmk_ep_t g_touch_dm_ep = ULMK_EP_INVALID;

static int dm_open(void *ctx)
{
	(void)ctx;
	return ULMK_OK;
}

static int dm_close(void *ctx)
{
	(void)ctx;
	return ULMK_OK;
}

static int dm_read(void *ctx, void *buf, size_t len, uint32_t flags)
{
	struct ulmk_input_event *ev;
	uint16_t x, y;
	int rc;

	(void)ctx;
	(void)flags;
	if (!buf || len < sizeof(*ev))
		return ULMK_EINVAL;

	x = 0u;
	y = 0u;
	rc = touch_poll(0u, &x, &y);
	if (rc < 0)
		return rc;

	ev = (struct ulmk_input_event *)buf;
	if (rc == 1) {
		ev->x = (int16_t)x;
		ev->y = (int16_t)y;
		ev->state = ULMK_INPUT_STATE_PRESSED;
	} else {
		ev->x = 0;
		ev->y = 0;
		ev->state = ULMK_INPUT_STATE_RELEASED;
	}
	ev->_pad[0] = 0u;
	ev->_pad[1] = 0u;
	ev->_pad[2] = 0u;
	return (int)sizeof(*ev);
}

static const struct ulmk_dev_ops g_touch_dm_ops = {
	.open = dm_open,
	.close = dm_close,
	.read = dm_read,
};

static void touch_dm_server(void *arg)
{
	(void)arg;
	ulmk_dev_serve(g_touch_dm_ep, &g_touch_dm_ops, NULL);
}

ulmk_ep_t touch_dm_ep(void)
{
	return g_touch_dm_ep;
}

ulmk_tid_t touch_dm_init(void)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (touch_ep() == ULMK_EP_INVALID) {
		if (touch_init(0u) == ULMK_TID_INVALID)
			return ULMK_TID_INVALID;
	}

	if (g_touch_dm_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_touch_dm_ep = ulmk_ep_create();
	if (g_touch_dm_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name = "touch_dm";
	attr.entry = touch_dm_server;
	attr.priority = 2u;
	attr.stack_size = 2048u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	return tid;
}
