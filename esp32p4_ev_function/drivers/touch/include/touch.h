/* SPDX-License-Identifier: MIT */
#ifndef TOUCH_H
#define TOUCH_H
#include <stdint.h>
#include <ulmk/microkernel.h>
ulmk_tid_t touch_init(uint8_t n);
int touch_read(int *x, int *y, int *pressed);
#endif
