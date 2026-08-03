/* SPDX-License-Identifier: MIT */
#ifndef DISPLAY_DRV_H
#define DISPLAY_DRV_H

#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"

#define DISPLAY_W		ULMK_BOARD_DISPLAY_W
#define DISPLAY_H		ULMK_BOARD_DISPLAY_H
#define DISPLAY_BPP		ULMK_BOARD_DISPLAY_BPP
#define DISPLAY_FB_BYTES	ULMK_BOARD_DISPLAY_FB_BYTES
#define DISPLAY_FB_MAP_SIZE	ULMK_BOARD_DISPLAY_FB_MAP_SIZE

ulmk_tid_t display_init(uint8_t mod);
ulmk_ep_t display_ep(void);
uint16_t *display_write(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
int display_flip(void);
int display_on(int on);
void *display_fb_base(void);
uint16_t *display_fb(unsigned idx);
/* Non-cacheable alias of a PSRAM FB — the only safe way to paint. */
uint16_t *display_fb_nc(uint16_t *cached);
int display_present(const void *fb);
uint16_t display_width(void);
uint16_t display_height(void);
int display_fb_in_psram(void);

#endif /* DISPLAY_DRV_H */
