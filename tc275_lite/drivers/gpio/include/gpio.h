/* SPDX-License-Identifier: MIT */
#ifndef GPIO_H
#define GPIO_H

#include <ulmk/microkernel.h>
#include <stdint.h>

/* Encoded pin: (port_num << 8) | pin — port_num 0 = P00. */
#define GPIO_PIN(port, pin) \
	((uint16_t)((((uint16_t)(port)) << 8) | ((uint16_t)(pin) & 0xFFu)))

#define GPIO_DIR_IN		0u
#define GPIO_DIR_OUT		1u

#define GPIO_PULL_NONE		0u
#define GPIO_PULL_UP		1u
#define GPIO_PULL_DOWN		2u

/* Alternate output function 1..7 (push-pull). 0 = general-purpose out. */
#define GPIO_ALT_GENERAL	0u

#define GPIO_EVT_NONE		0u
#define GPIO_EVT_FALLING	1u
#define GPIO_EVT_RISING		2u
#define GPIO_EVT_BOTH		3u

/*
 * gpio_init — start the GPIO IPC + IRQ server (once).
 * Returns server tid, or ULMK_TID_INVALID.
 */
ulmk_tid_t gpio_init(void);

int gpio_config(uint16_t pin, uint32_t dir, uint32_t pull, uint32_t alt);
int gpio_set(uint16_t pin, int value);
int gpio_get(uint16_t pin, int *value);

/*
 * gpio_subscribe — register for pin-edge notifications (non-blocking).
 *
 * Performs one ep_call to the GPIO server, stores (notif, bit), and returns
 * immediately.  When the pin's hardware IRQ fires, the server defers by
 * signaling every matching subscriber's notification — the client blocks on
 * ulmk_notif_wait(), never on the subscribe call itself.
 *
 * Lite Kit: Button1 (P00.7) is IRQ-capable via GTM TIM2 CH6.
 * @p bit is a bit index 0..31 (the server signals @c 1u << bit).
 */
int gpio_subscribe(uint16_t pin, uint32_t edge, ulmk_notif_t n, uint32_t bit);

/*
 * gpio_irq_kick — run the same defer path as a hardware GPIO IRQ (HIL/test).
 * Does not touch the pin; signals every active subscriber for Button1.
 */
int gpio_irq_kick(void);

#endif /* GPIO_H */
