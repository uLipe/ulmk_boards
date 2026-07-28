/* SPDX-License-Identifier: MIT */
#ifndef PORT_DISP_H
#define PORT_DISP_H

#include <lvgl.h>

lv_display_t *port_disp_init(void);

/*
 * Zero both FBs, clear DIRECT sync_areas, reset buf_act, present inactive.
 * Call before the first benchmark scene after the splash.
 */
void port_disp_blank(void);

#endif /* PORT_DISP_H */
