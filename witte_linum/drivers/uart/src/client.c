/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "uart_internal.h"

static int uart_call(uint8_t n, ulmk_msg_t *msg)
{
	if (n >= UART_MAX || g_uart_eps[n] == ULMK_EP_INVALID)
		return ULMK_ESRCH;
	return ulmk_ep_call(g_uart_eps[n], msg);
}

int uart_tx_byte(uint8_t n, uint8_t byte)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = UART_MSG_TX_BYTE;
	msg.words[0] = byte;
	rc = uart_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}

int uart_tx_buf(uint8_t n, const uint8_t *buf, size_t len)
{
	size_t i;
	int rc;

	if (!buf)
		return ULMK_EINVAL;
	for (i = 0u; i < len; i++) {
		rc = uart_tx_byte(n, buf[i]);
		if (rc != ULMK_OK)
			return rc;
	}
	return ULMK_OK;
}

int uart_rx_byte(uint8_t n, uint8_t *out)
{
	ulmk_msg_t msg;
	int rc;

	msg.label = UART_MSG_RX_BYTE;
	rc = uart_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	if (out)
		*out = (uint8_t)msg.words[1];
	return ULMK_OK;
}

int uart_rx_byte_nb(uint8_t n, uint8_t *out)
{
	ulmk_msg_t msg;
	int rc;

	msg.label = UART_MSG_RX_BYTE_NB;
	rc = uart_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	if ((int)(int32_t)msg.words[0] != ULMK_OK)
		return (int)(int32_t)msg.words[0];
	if (out)
		*out = (uint8_t)msg.words[1];
	return ULMK_OK;
}

int uart_set_baud(uint8_t n, uint32_t baud)
{
	ulmk_msg_t msg;
	int rc;

	msg.label    = UART_MSG_SET_BAUD;
	msg.words[0] = baud;
	rc = uart_call(n, &msg);
	if (rc != ULMK_OK)
		return rc;
	return (int)(int32_t)msg.words[0];
}
