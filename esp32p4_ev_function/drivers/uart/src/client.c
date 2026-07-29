/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stddef.h>
#include "uart.h"
#include "uart_internal.h"

static ulmk_ep_t inst_ep(uint8_t n)
{
	if (n >= UART_MAX_INST)
		return ULMK_EP_INVALID;
	return g_uart_eps[n];
}

int uart_tx_byte(uint8_t n, uint8_t b)
{
	ulmk_msg_t msg;
	ulmk_ep_t ep = inst_ep(n);
	int rc;

	if (ep == ULMK_EP_INVALID)
		return ULMK_EINVAL;
	msg.label = UART_MSG_TXB;
	msg.words[0] = b;
	rc = ulmk_ep_call(ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int uart_tx_buf(uint8_t n, const uint8_t *buf, size_t len)
{
	ulmk_msg_t msg;
	ulmk_ep_t ep = inst_ep(n);
	int rc;

	if (ep == ULMK_EP_INVALID || !buf || len == 0u)
		return ULMK_EINVAL;
	msg.label = UART_MSG_TXBUF;
	msg.words[0] = (uint32_t)(uintptr_t)buf;
	msg.words[1] = (uint32_t)len;
	rc = ulmk_ep_call(ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int uart_rx_byte(uint8_t n, uint8_t *b)
{
	ulmk_msg_t msg;
	ulmk_ep_t ep = inst_ep(n);
	int rc;

	if (ep == ULMK_EP_INVALID || !b)
		return ULMK_EINVAL;
	msg.label = UART_MSG_RXB;
	rc = ulmk_ep_call(ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	*b = (uint8_t)msg.words[1];
	return ULMK_OK;
}

int uart_set_baud(uint8_t n, uint32_t baud)
{
	ulmk_msg_t msg;
	ulmk_ep_t ep = inst_ep(n);
	int rc;

	if (ep == ULMK_EP_INVALID)
		return ULMK_EINVAL;
	msg.label = UART_MSG_BAUD;
	msg.words[0] = baud;
	rc = ulmk_ep_call(ep, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}
