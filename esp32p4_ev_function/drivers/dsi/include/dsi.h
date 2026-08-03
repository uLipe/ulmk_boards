/* SPDX-License-Identifier: MIT */
#ifndef DSI_DRV_H
#define DSI_DRV_H

/*
 * MIPI-DSI + EK79007.
 * dsi_init() — PHY + DCS in command mode (no video yet; no VPG).
 * dsi_video_enable() — host video + AUTO clock (call only after DMA feeds).
 * dsi_fb_start() — DW_GDMA→DPI, then video_enable + dpi_en (IDF order).
 */
int dsi_init(void);
int dsi_ready(void);
void dsi_video_enable(void);
void dsi_host_dump(void);

/* Stream FB to panel (requires AXI PSRAM). nbytes >= 1024*600*2. */
int dsi_fb_start(void *fb, uint32_t nbytes);
int dsi_fb_set(void *fb);
/*
 * Point DMA at @fb and block until the next DW_GDMA rearm consumes it.
 * Requires dsi_fb_start() first.  Returns 0 or -1.
 */
int dsi_fb_present(void *fb);
int dsi_fb_ready(void);

/* Sample DMA frame rate + dump DMA/bridge/host state over window_ms. */
void dsi_fb_diag(uint32_t window_ms);
/* Free-running frame counter (60 Hz once streaming) — cheap timebase. */
uint32_t dsi_fb_frames(void);
uint32_t dsi_fb_present_timeouts(void);

#endif /* DSI_DRV_H */
