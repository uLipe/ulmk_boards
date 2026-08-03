/* SPDX-License-Identifier: MIT */
/*
 * can_dm — ulmk_dev_serve adapter over legacy can_send / can_recv.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_can.h>
#include <can.h>
#include "can_dm.h"
#include "board_config.h"

#define CAN_DM_STACK	2048u

struct can_dm_ctx {
	uint8_t  n;
	uint8_t  opened;
	uint8_t  loopback;
	uint32_t bitrate;
};

static struct can_dm_ctx g_ctx[CAN_MAX] __attribute__((section(".user_bss")));
static ulmk_ep_t g_eps[CAN_MAX] __attribute__((section(".user_bss")));
static ulmk_tid_t g_tids[CAN_MAX] __attribute__((section(".user_bss")));

static int dm_open(void *arg)
{
	struct can_dm_ctx *c = arg;

	if (!c || c->n >= CAN_MAX)
		return ULMK_EINVAL;
	if (!c->opened)
		return ULMK_EINVAL;
	return ULMK_OK;
}

static int dm_close(void *arg)
{
	struct can_dm_ctx *c = arg;

	(void)c;
	return ULMK_OK;
}

static int dm_write(void *arg, const void *buf, size_t len, uint32_t flags)
{
	struct can_dm_ctx *c = arg;
	const struct ulmk_can_frame *in;
	can_frame_t fr;

	(void)flags;
	if (!c || !buf || len < sizeof(*in))
		return ULMK_EINVAL;
	in = (const struct ulmk_can_frame *)buf;
	if (in->dlc > 8u)
		return ULMK_EINVAL;

	fr.id = in->id;
	fr.dlc = in->dlc;
	memcpy(fr.data, in->data, 8u);
	return can_send(c->n, &fr);
}

static int dm_read(void *arg, void *buf, size_t len, uint32_t flags)
{
	struct can_dm_ctx *c = arg;
	struct ulmk_can_frame *out;
	can_frame_t fr;
	int rc;

	(void)flags;
	if (!c || !buf || len < sizeof(*out))
		return ULMK_EINVAL;
	out = (struct ulmk_can_frame *)buf;

	rc = can_recv(c->n, &fr);
	if (rc != ULMK_OK)
		return rc;

	out->id = fr.id;
	out->dlc = fr.dlc;
	memcpy(out->data, fr.data, 8u);
	out->_pad[0] = 0u;
	out->_pad[1] = 0u;
	out->_pad[2] = 0u;
	return (int)sizeof(*out);
}

static const struct ulmk_dev_ops g_ops = {
	.open = dm_open,
	.close = dm_close,
	.read = dm_read,
	.write = dm_write,
	.info = NULL,
	.ioctl = NULL,
	.submit = NULL,
	.wait = NULL,
};

static void can_dm_server(void *arg)
{
	struct can_dm_ctx *c = arg;

	ulmk_dev_serve(g_eps[c->n], &g_ops, c);
}

ulmk_ep_t can_dm_ep(uint8_t n)
{
	if (n >= CAN_MAX)
		return ULMK_EP_INVALID;
	return g_eps[n];
}

ulmk_tid_t can_dm_init(uint8_t n, uint32_t bitrate, int loopback)
{
	ulmk_thread_attr_t attr = { 0 };
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	ulmk_tid_t hw;

	if (n >= CAN_MAX)
		return ULMK_TID_INVALID;
	if (g_eps[n] != ULMK_EP_INVALID)
		return g_tids[n];

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_ctx[n].n = n;
	g_ctx[n].opened = 0u;
	g_ctx[n].loopback = loopback ? 1u : 0u;
	g_ctx[n].bitrate = bitrate ? bitrate : 500000u;
	g_eps[n] = ep;

	attr.name = "can_dm";
	attr.entry = can_dm_server;
	attr.arg = &g_ctx[n];
	attr.priority = 2u;
	attr.stack_size = CAN_DM_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		g_eps[n] = ULMK_EP_INVALID;
		return ULMK_TID_INVALID;
	}
	g_tids[n] = tid;

	hw = can_init(n, bitrate ? bitrate : 500000u, loopback);
	if (hw == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	g_ctx[n].opened = 1u;
	return tid;
}
