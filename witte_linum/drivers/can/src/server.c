/* SPDX-License-Identifier: MIT */
/*
 * FDCAN has no STM32H7 Cube LL header.  This uses only CMSIS register
 * definitions; no HAL object or HAL API is linked.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_ll.h"
#include "can_internal.h"
#include "pinmux_internal.h"

#define CAN_STACK_SIZE		2048u
#define CAN_RAM_BASE		0x4000AC00u
#define CAN_RAM_SIZE		0x2800u
#define CAN_RAM_STRIDE_WORDS	32u
#define CAN_NOTIF_MASK		(1u << CAN_NOTIF_IRQ)

struct can_args {
	uint8_t n;
	uint8_t srpn;
	uint8_t nvic;
	uint32_t bitrate;
	uint32_t flags;
	ulmk_ep_t ep;
	ulmk_notif_t notif;
	FDCAN_GlobalTypeDef *regs;
	volatile uint32_t *ram;
};

ulmk_ep_t g_can_eps[CAN_MAX];
static struct can_args g_can[CAN_MAX] __attribute__((section(".user_bss")));

static void can_pinmux(uint8_t n)
{
	pinmux_cfg_t cfg;
	GPIO_TypeDef *std_gpio;
	uint32_t std_pin;

	std_gpio = pinmux_gpio(n == 0u ? ULMK_BOARD_CAN_STD1_PORT :
			       ULMK_BOARD_CAN_STD2_PORT);
	std_pin = 1u << (n == 0u ? ULMK_BOARD_CAN_STD1_PIN :
			 ULMK_BOARD_CAN_STD2_PIN);
	if (std_gpio) {
		LL_GPIO_SetPinMode(std_gpio, std_pin, LL_GPIO_MODE_OUTPUT);
		LL_GPIO_ResetOutputPin(std_gpio, std_pin);
	}
	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = 9u;
	cfg.flags = 0u;
	if (n == 0u) {
		cfg.port = 7u;
		cfg.pin = 13u;
		(void)pinmux_apply(&cfg);
		cfg.pin = 14u;
		cfg.dir = PINMUX_DIR_IN;
		cfg.pull = PINMUX_PULL_UP;
		(void)pinmux_apply(&cfg);
	} else {
		cfg.port = 1u;
		cfg.pin = 12u;
		(void)pinmux_apply(&cfg);
		cfg.pin = 13u;
		cfg.dir = PINMUX_DIR_IN;
		cfg.pull = PINMUX_PULL_UP;
		(void)pinmux_apply(&cfg);
	}
}

static int can_hw_init(struct can_args *a)
{
	uint32_t ram_base;

	if (a->bitrate != 500000u)
		return ULMK_EINVAL;
	can_pinmux(a->n);
	a->regs->CCCR |= FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;
	a->regs->NBTP = (29u << 16) | (1u << 8) | 12u;
	a->regs->RXESC = 0u;
	a->regs->TXESC = 0u;
	ram_base = (uint32_t)a->n * CAN_RAM_STRIDE_WORDS;
	a->regs->SIDFC = ram_base << 2;
	a->regs->XIDFC = 0u;
	a->regs->RXF0C = ((ram_base + 8u) << 2) | (1u << 16);
	a->regs->TXBC = ((ram_base + 16u) << 2) | (1u << 16);
	a->regs->GFC = 0u;
	a->regs->IR = 0xFFFFFFFFu;
	a->regs->IE = FDCAN_IE_RF0NE;
	if (a->flags & CAN_FLAG_LOOPBACK) {
		a->regs->CCCR |= FDCAN_CCCR_TEST;
		a->regs->TEST |= FDCAN_TEST_LBCK;
	}
	a->regs->CCCR &= ~(FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);
	return ULMK_OK;
}

static int can_hw_send(struct can_args *a, const ulmk_msg_t *msg)
{
	volatile uint32_t *tx = a->ram + (uint32_t)a->n * CAN_RAM_STRIDE_WORDS + 16u;

	if (msg->words[0] > 0x7FFu || msg->words[1] > 8u)
		return ULMK_EINVAL;
	if (a->regs->TXFQS & (1u << 21))
		return ULMK_ETIMEOUT;
	tx[0] = msg->words[0] << 18;
	tx[1] = msg->words[1] << 16;
	tx[2] = msg->words[2];
	tx[3] = msg->words[3];
	a->regs->TXBAR = 1u;
	return ULMK_OK;
}

static int can_hw_recv(struct can_args *a, ulmk_msg_t *reply)
{
	volatile uint32_t *rx;
	uint32_t bits;
	int ret;

	if ((a->regs->RXF0S & 0x7Fu) == 0u) {
		ulmk_irq_ack(a->srpn);
		bits = 0u;
		ret = ulmk_notif_wait(a->notif, CAN_NOTIF_MASK, &bits);
		if (ret != ULMK_OK)
			return ret;
	}
	if ((a->regs->RXF0S & 0x7Fu) == 0u)
		return ULMK_ETIMEOUT;
	rx = a->ram + (uint32_t)a->n * CAN_RAM_STRIDE_WORDS + 8u;
	reply->words[1] = ((rx[1] >> 16) << 16) | ((rx[0] >> 18) & 0x7FFu);
	reply->words[2] = rx[2];
	reply->words[3] = rx[3];
	a->regs->RXF0A = a->regs->RXF0S >> 8;
	a->regs->IR = FDCAN_IR_RF0N;
	ulmk_irq_ack(a->srpn);
	return ULMK_OK;
}

static void can_server(void *arg)
{
	struct can_args *a = arg;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	a->regs = ulmk_mem_map((void *)(uintptr_t)(a->n == 0u ?
			ULMK_BOARD_FDCAN1_BASE : ULMK_BOARD_FDCAN2_BASE),
			ULMK_BOARD_FDCAN_MAP_SIZE, ULMK_PERM_READ | ULMK_PERM_WRITE,
			ULMK_MMAP_PERIPH);
	a->ram = ulmk_mem_map((void *)CAN_RAM_BASE, CAN_RAM_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!a->regs || !a->ram)
		return;
	if (can_hw_init(a) != ULMK_OK)
		return;
	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		if (msg.label == CAN_MSG_SEND)
			reply.words[0] = (uint32_t)can_hw_send(a, &msg);
		else if (msg.label == CAN_MSG_RECV)
			reply.words[0] = (uint32_t)can_hw_recv(a, &reply);
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t can_init(uint8_t n, uint32_t bitrate, uint32_t flags)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	int ret;

	if (n >= CAN_MAX || g_can_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_can[n].ep = ulmk_ep_create();
	g_can[n].notif = ulmk_notif_create();
	if (g_can[n].ep == ULMK_EP_INVALID ||
	    g_can[n].notif == ULMK_NOTIF_INVALID)
		return ULMK_TID_INVALID;
	g_can[n].n = n;
	g_can[n].bitrate = bitrate;
	g_can[n].flags = flags;
	g_can[n].srpn = n == 0u ? ULMK_BOARD_IRQ_FDCAN1 : ULMK_BOARD_IRQ_FDCAN2;
	g_can[n].nvic = n == 0u ? ULMK_BOARD_NVIC_FDCAN1_IT0 :
				      ULMK_BOARD_NVIC_FDCAN2_IT0;
	ret = ulmk_irq_bind_hw(g_can[n].srpn, g_can[n].notif, CAN_NOTIF_IRQ,
			       ULMK_BOARD_NVIC_SRC(g_can[n].nvic));
	if (ret != ULMK_OK || ulmk_irq_enable(g_can[n].srpn) != ULMK_OK)
		return ULMK_TID_INVALID;
	attr.name = "can";
	attr.entry = can_server;
	attr.arg = &g_can[n];
	attr.priority = 2u;
	attr.stack_size = CAN_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	g_can_eps[n] = g_can[n].ep;
	return tid;
}
