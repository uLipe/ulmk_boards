/* SPDX-License-Identifier: MIT */
/*
 * FT5446 touch — EXTI9 (PH9) + I2C read in server (keeps MPU maps off root).
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <i2c.h>
#include "board_config.h"
#include "board_ll.h"
#include "touch_internal.h"
#include "pinmux_internal.h"

#define TOUCH_STACK_SIZE	2048u
#define EXTI_IMR1_OFF		0x080u
#define EXTI_PR1_OFF		0x088u
#define EXTI_FTSR1_OFF		0x004u
#define SYSCFG_EXTICR3_OFF	0x10u

ulmk_ep_t g_touch_ep;
static ulmk_notif_t g_touch_notif __attribute__((section(".user_bss")));
static volatile uint32_t *g_exti_imr1 __attribute__((section(".user_bss")));
static volatile uint32_t *g_exti_pr1 __attribute__((section(".user_bss")));
static volatile uint32_t *g_exti_ftsr1 __attribute__((section(".user_bss")));
static volatile uint32_t *g_syscfg_exticr3 __attribute__((section(".user_bss")));
static GPIO_TypeDef *g_int_gpio __attribute__((section(".user_bss")));

static void touch_exti_ack(void)
{
	if (g_exti_pr1)
		*g_exti_pr1 = (1u << ULMK_BOARD_TOUCH_INT_PIN);
	ulmk_irq_ack(ULMK_BOARD_IRQ_TOUCH_EXTI);
}

static void touch_exti_mask(void)
{
	if (g_exti_imr1)
		*g_exti_imr1 &= ~(1u << ULMK_BOARD_TOUCH_INT_PIN);
}

static void touch_exti_unmask(void)
{
	if (g_exti_imr1)
		*g_exti_imr1 |= (1u << ULMK_BOARD_TOUCH_INT_PIN);
}

static int touch_int_is_low(void)
{
	if (!g_int_gpio)
		return 0;
	return (g_int_gpio->IDR & (1u << ULMK_BOARD_TOUCH_INT_PIN)) == 0u;
}

static int touch_hw_init(void)
{
	pinmux_cfg_t cfg;
	void *exti;
	void *syscfg;

	cfg.port = ULMK_BOARD_TOUCH_INT_PORT;
	cfg.pin = ULMK_BOARD_TOUCH_INT_PIN;
	cfg.dir = PINMUX_DIR_IN;
	cfg.pull = PINMUX_PULL_UP;
	cfg.alt = PINMUX_ALT_GPIO;
	cfg.flags = 0u;
	if (pinmux_apply(&cfg) != ULMK_OK)
		return ULMK_EINVAL;

	g_int_gpio = pinmux_gpio(ULMK_BOARD_TOUCH_INT_PORT);
	if (!g_int_gpio)
		return ULMK_ENOMEM;

	exti = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_EXTI_BASE,
			    ULMK_BOARD_EXTI_MAP_SIZE,
			    ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	syscfg = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_SYSCFG_BASE,
			      ULMK_BOARD_EXTI_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!exti || !syscfg)
		return ULMK_ENOMEM;

	g_exti_imr1 = (volatile uint32_t *)((uintptr_t)exti + EXTI_IMR1_OFF);
	g_exti_pr1 = (volatile uint32_t *)((uintptr_t)exti + EXTI_PR1_OFF);
	g_exti_ftsr1 = (volatile uint32_t *)((uintptr_t)exti + EXTI_FTSR1_OFF);
	g_syscfg_exticr3 =
		(volatile uint32_t *)((uintptr_t)syscfg + SYSCFG_EXTICR3_OFF);

	*g_syscfg_exticr3 = (*g_syscfg_exticr3 & ~(0xFu << 4)) | (7u << 4);
	*g_exti_ftsr1 |= (1u << ULMK_BOARD_TOUCH_INT_PIN);
	touch_exti_ack();
	touch_exti_unmask();
	return ULMK_OK;
}

static int touch_do_wait(uint32_t timeout_ms)
{
	uint32_t bits = 0u;
	int ret;

	/*
	 * Always block on EXTI (or timeout). Returning OK while INT is still
	 * low would spin the client in a tight ep_call loop.
	 */
	touch_exti_ack();
	touch_exti_unmask();
	if (timeout_ms == 0u)
		ret = ulmk_notif_wait(g_touch_notif, 1u << TOUCH_NOTIF_EXTI,
				      &bits);
	else
		ret = ulmk_notif_wait_timeout(g_touch_notif,
					     1u << TOUCH_NOTIF_EXTI, &bits,
					     timeout_ms);
	touch_exti_ack();
	touch_exti_mask();
	return ret;
}

static int touch_do_read_xy(uint16_t *x, uint16_t *y)
{
	uint8_t reg = FT5X06_REG_TD_STATUS;
	uint8_t buf[7];
	uint16_t raw_x;
	uint16_t raw_y;
	int ret;
	uint32_t tries;

	ret = i2c_writeread(0u, ULMK_BOARD_TOUCH_ADDR7, &reg, 1u, buf, 7u);
	if (ret != ULMK_OK)
		return ret;
	if ((buf[0] & 0x0Fu) == 0u)
		return ULMK_ESRCH;
	raw_x = (uint16_t)(((uint16_t)(buf[1] & 0x0Fu) << 8) | buf[2]);
	raw_y = (uint16_t)(((uint16_t)(buf[3] & 0x0Fu) << 8) | buf[4]);
	/* Native landscape: raw axes match LTDC 1024×600 (no swap-xy). */
	*x = raw_x;
	*y = raw_y;

	/* Wait for INT release before next edge (sleep, not busy-wait). */
	for (tries = 0u; tries < 50u; tries++) {
		if (!touch_int_is_low())
			break;
		(void)ulmk_sleep_ms(10u);
	}
	touch_exti_ack();
	return ULMK_OK;
}

/*
 * Non-blocking sample for LVGL indev: INT low → I2C read (no release wait).
 * Returns 1 pressed, 0 released, or negative errno.
 */
static int touch_do_poll(uint16_t *x, uint16_t *y)
{
	uint8_t reg = FT5X06_REG_TD_STATUS;
	uint8_t buf[7];
	uint16_t raw_x;
	uint16_t raw_y;
	int ret;

	if (!touch_int_is_low())
		return 0;

	ret = i2c_writeread(0u, ULMK_BOARD_TOUCH_ADDR7, &reg, 1u, buf, 7u);
	if (ret != ULMK_OK)
		return ret;
	if ((buf[0] & 0x0Fu) == 0u)
		return 0;
	raw_x = (uint16_t)(((uint16_t)(buf[1] & 0x0Fu) << 8) | buf[2]);
	raw_y = (uint16_t)(((uint16_t)(buf[3] & 0x0Fu) << 8) | buf[4]);
	*x = raw_x;
	*y = raw_y;
	touch_exti_ack();
	return 1;
}

static void touch_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint16_t x;
	uint16_t y;

	(void)arg;
	if (touch_hw_init() != ULMK_OK)
		return;

	for (;;) {
		if (ulmk_ep_recv(g_touch_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		if (msg.label == TOUCH_MSG_WAIT) {
			reply.words[0] = (uint32_t)touch_do_wait(msg.words[0]);
		} else if (msg.label == TOUCH_MSG_READ_XY) {
			reply.words[0] = (uint32_t)touch_do_read_xy(&x, &y);
			if ((int)(int32_t)reply.words[0] == ULMK_OK) {
				reply.words[1] = x;
				reply.words[2] = y;
			}
		} else if (msg.label == TOUCH_MSG_POLL) {
			reply.words[0] = (uint32_t)touch_do_poll(&x, &y);
			if ((int)(int32_t)reply.words[0] == 1) {
				reply.words[1] = x;
				reply.words[2] = y;
			}
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t touch_init(uint8_t n)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (n != 0u || g_touch_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_touch_ep = ulmk_ep_create();
	g_touch_notif = ulmk_notif_create();
	if (g_touch_ep == ULMK_EP_INVALID ||
	    g_touch_notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;
	if (ulmk_irq_bind_hw(ULMK_BOARD_IRQ_TOUCH_EXTI, g_touch_notif,
			     TOUCH_NOTIF_EXTI,
			     ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_EXTI9_5)) !=
	    ULMK_OK)
		return ULMK_TID_INVALID;
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_TOUCH_EXTI) != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name = "touch";
	attr.entry = touch_server;
	attr.priority = 2u;
	attr.stack_size = TOUCH_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
