/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_display.h>
#include <display.h>
#include <display_dm.h>
#include "board_cache.h"

static ulmk_ep_t g_display_dm_ep = ULMK_EP_INVALID;

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

static int dm_info(void *ctx, uint32_t *out, uint32_t nout)
{
	uint32_t w;
	uint32_t h;

	(void)ctx;
	if (!out || nout < 5u)
		return ULMK_EINVAL;
	w = display_width();
	h = display_height();
	out[0] = w;
	out[1] = h;
	out[2] = 16u;
	out[3] = w * 2u;
	out[4] = 2u;
	return ULMK_OK;
}

static int dm_ioctl(void *ctx, uint32_t req, uint32_t *args, uint32_t nargs)
{
	void *fb;
	int rc;

	(void)ctx;
	if (!args)
		return ULMK_EINVAL;

	switch (req) {
	case ULMK_DISP_IOCTL_GET_FB:
		if (nargs < 1u)
			return ULMK_EINVAL;
		fb = display_fb(args[0]);
		if (!fb) {
			args[0] = (uint32_t)(int32_t)ULMK_EINVAL;
			return ULMK_EINVAL;
		}
		args[0] = (uint32_t)ULMK_OK;
		args[1] = (uint32_t)(uintptr_t)fb;
		return ULMK_OK;

	case ULMK_DISP_IOCTL_ON:
		rc = display_on(args[0] ? 1 : 0);
		args[0] = (uint32_t)(int32_t)rc;
		return rc;

	default:
		return ULMK_ENOTSUP;
	}
}

static int dm_write(void *ctx, const void *buf, size_t len, uint32_t flags)
{
	const struct ulmk_disp_present_hdr *hdr;
	const struct ulmk_disp_rect *rects;
	void *fb;
	uint32_t stride;
	uint32_t fb_bytes;
	uint32_t i;
	uint32_t n;

	(void)ctx;
	(void)flags;
	if (!buf || len < sizeof(*hdr))
		return ULMK_EINVAL;

	hdr = (const struct ulmk_disp_present_hdr *)buf;
	fb = (void *)(uintptr_t)hdr->fb_ptr;
	if (!fb)
		return ULMK_EINVAL;

	n = hdr->n_rects;
	if (n > 0u) {
		if (len < sizeof(*hdr) +
		    (size_t)n * sizeof(struct ulmk_disp_rect))
			return ULMK_EINVAL;
		rects = (const struct ulmk_disp_rect *)(hdr + 1);
		stride = (uint32_t)display_width() * 2u;
		for (i = 0u; i < n; i++) {
			board_dcache_clean_area(fb, stride,
						rects[i].x, rects[i].y,
						rects[i].w, rects[i].h,
						2u);
		}
	} else {
		fb_bytes = (uint32_t)display_width() *
			   (uint32_t)display_height() * 2u;
		board_dcache_clean(fb, fb_bytes);
	}

	return display_present(fb);
}

static const struct ulmk_dev_ops g_display_dm_ops = {
	.open = dm_open,
	.close = dm_close,
	.write = dm_write,
	.info = dm_info,
	.ioctl = dm_ioctl,
};

static void display_dm_server(void *arg)
{
	(void)arg;
	ulmk_dev_serve(g_display_dm_ep, &g_display_dm_ops, NULL);
}

ulmk_ep_t display_dm_ep(void)
{
	return g_display_dm_ep;
}

ulmk_tid_t display_dm_init(void)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (display_ep() == ULMK_EP_INVALID) {
		if (display_init(0u) == ULMK_TID_INVALID)
			return ULMK_TID_INVALID;
	}

	if (g_display_dm_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_display_dm_ep = ulmk_ep_create();
	if (g_display_dm_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	attr.name = "disp_dm";
	attr.entry = display_dm_server;
	attr.priority = 2u;
	attr.stack_size = 4096u;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	return tid;
}
