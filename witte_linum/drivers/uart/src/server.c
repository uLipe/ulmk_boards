/* SPDX-License-Identifier: MIT */
/*
 * uart server — USART via STM32 LL; TXE/RXNE wait on NVIC IRQ + notif.
 * USART1 clocks enabled in board_init.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "uart_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"
#include "board_ll.h"

#define UART_STACK_SIZE		2048u

typedef struct {
	uint8_t      n;
	uart_pins_t  pins;
	uint32_t     baud;
	uint32_t     pclk_hz;
	ulmk_ep_t    ep;
	ulmk_notif_t irq_notif;
	uint8_t      srpn;
} uart_args_t;

ulmk_ep_t g_uart_eps[UART_MAX];
static uart_args_t g_args[UART_MAX] __attribute__((section(".user_bss")));

static USART_TypeDef *uart_reg(uint8_t n)
{
	if (n == 0u)
		return USART1;
	return NULL;
}

static void pinmux_uart(const uart_args_t *a)
{
	pinmux_cfg_t cfg;

	cfg.port  = a->pins.tx_port;
	cfg.pin   = a->pins.tx_pin;
	cfg.dir   = PINMUX_DIR_OUT;
	cfg.pull  = PINMUX_PULL_NONE;
	cfg.alt   = a->pins.af;
	cfg.flags = 0u;
	(void)pinmux_apply(&cfg);

	cfg.port  = a->pins.rx_port;
	cfg.pin   = a->pins.rx_pin;
	cfg.dir   = PINMUX_DIR_IN;
	cfg.pull  = PINMUX_PULL_UP;
	cfg.alt   = a->pins.af;
	cfg.flags = 0u;
	(void)pinmux_apply(&cfg);
}

static void hw_init(USART_TypeDef *us, uint32_t baud, uint32_t pclk)
{
	LL_USART_Disable(us);
	LL_USART_SetTransferDirection(us, LL_USART_DIRECTION_TX_RX);
	LL_USART_ConfigCharacter(us, LL_USART_DATAWIDTH_8B,
				 LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
	LL_USART_SetBaudRate(us, pclk, LL_USART_PRESCALER_DIV1,
			     LL_USART_OVERSAMPLING_16, baud);
	LL_USART_DisableIT_TXE(us);
	LL_USART_DisableIT_RXNE(us);
	LL_USART_Enable(us);
}

static int wait_txe(uart_args_t *a, USART_TypeDef *us)
{
	uint32_t bits;
	int ret;

	if (LL_USART_IsActiveFlag_TXE(us))
		return ULMK_OK;

	LL_USART_ClearFlag_TC(us);
	ulmk_irq_ack(a->srpn);
	LL_USART_EnableIT_TXE(us);

	bits = 0u;
	ret = ulmk_notif_wait(a->irq_notif, 1u << UART_NOTIF_IRQ, &bits);

	LL_USART_DisableIT_TXE(us);
	ulmk_irq_ack(a->srpn);
	return ret;
}

static int wait_rxne(uart_args_t *a, USART_TypeDef *us)
{
	uint32_t bits;
	int ret;

	if (LL_USART_IsActiveFlag_RXNE(us))
		return ULMK_OK;

	ulmk_irq_ack(a->srpn);
	LL_USART_EnableIT_RXNE(us);

	bits = 0u;
	ret = ulmk_notif_wait(a->irq_notif, 1u << UART_NOTIF_IRQ, &bits);

	LL_USART_DisableIT_RXNE(us);
	ulmk_irq_ack(a->srpn);
	return ret;
}

static int hw_tx(uart_args_t *a, USART_TypeDef *us, uint8_t byte)
{
	int ret;

	ret = wait_txe(a, us);
	if (ret != ULMK_OK)
		return ret;
	LL_USART_TransmitData8(us, byte);
	return ULMK_OK;
}

static int hw_rx(uart_args_t *a, USART_TypeDef *us, uint8_t *out, int blocking)
{
	int ret;

	if (LL_USART_IsActiveFlag_RXNE(us)) {
		*out = LL_USART_ReceiveData8(us);
		return ULMK_OK;
	}
	if (!blocking)
		return ULMK_ETIMEOUT;

	ret = wait_rxne(a, us);
	if (ret != ULMK_OK)
		return ret;
	if (!LL_USART_IsActiveFlag_RXNE(us))
		return ULMK_ETIMEOUT;
	*out = LL_USART_ReceiveData8(us);
	return ULMK_OK;
}

static void uart_server(void *arg)
{
	uart_args_t *a = (uart_args_t *)arg;
	USART_TypeDef *us;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t b;

	us = uart_reg(a->n);
	if (!us)
		for (;;)
			;

	mapped = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_USART1_BASE,
			      ULMK_BOARD_USART1_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	us = (USART_TypeDef *)mapped;

	pinmux_uart(a);
	hw_init(us, a->baud, a->pclk_hz);

	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;
		b = 0u;
		switch (msg.label) {
		case UART_MSG_TX_BYTE:
			reply.words[0] = (uint32_t)hw_tx(a, us,
						(uint8_t)msg.words[0]);
			break;
		case UART_MSG_RX_BYTE:
			reply.words[0] = (uint32_t)hw_rx(a, us, &b, 1);
			reply.words[1] = b;
			break;
		case UART_MSG_RX_BYTE_NB:
			reply.words[0] = (uint32_t)hw_rx(a, us, &b, 0);
			reply.words[1] = b;
			break;
		case UART_MSG_SET_BAUD:
			a->baud = msg.words[0];
			LL_USART_SetBaudRate(us, a->pclk_hz,
					     LL_USART_PRESCALER_DIV1,
					     LL_USART_OVERSAMPLING_16,
					     a->baud);
			reply.words[0] = (uint32_t)ULMK_OK;
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t uart_init(uint8_t n, const uart_pins_t *pins,
		     uint32_t baud, uint32_t pclk_hz)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	ulmk_notif_t notif;
	uart_pins_t def;
	int ret;
	uintptr_t src;

	if (n >= UART_MAX)
		return ULMK_TID_INVALID;
	if (g_uart_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	if (n != 0u)
		return ULMK_TID_INVALID;

	if (!pins) {
		def.tx_port = ULMK_BOARD_USART1_TX_PORT;
		def.tx_pin  = ULMK_BOARD_USART1_TX_PIN;
		def.rx_port = ULMK_BOARD_USART1_RX_PORT;
		def.rx_pin  = ULMK_BOARD_USART1_RX_PIN;
		def.af      = ULMK_BOARD_USART1_AF;
		pins = &def;
	}

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	notif = ulmk_notif_create();
	if (notif == ULMK_NOTIF_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}

	g_args[n].n = n;
	g_args[n].pins = *pins;
	g_args[n].baud = baud;
	g_args[n].pclk_hz = pclk_hz;
	g_args[n].ep = ep;
	g_args[n].irq_notif = notif;
	g_args[n].srpn = ULMK_BOARD_IRQ_USART1;

	src = (uintptr_t)ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_USART1);
	ret = ulmk_irq_bind_hw(g_args[n].srpn, notif, UART_NOTIF_IRQ, src);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(g_args[n].srpn);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	attr.name       = "uart";
	attr.entry      = uart_server;
	attr.arg        = &g_args[n];
	attr.priority   = 1u;
	attr.stack_size = UART_STACK_SIZE;
	attr.privilege  = ULMK_PRIV_DRIVER;

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	g_uart_eps[n] = ep;
	return tid;
}
