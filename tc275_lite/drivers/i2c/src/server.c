/* SPDX-License-Identifier: MIT */
/*
 * i2c server — MODULE_I2Cn via iLLD SFR (CLC in board_init). Soft transfer
 * path until full master FSM is wired; pinmux uses iLLD PORT modes.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include "i2c_internal.h"
#include "drivers/common/illd_port.h"
#include "IfxI2c_reg.h"

#define I2C_STACK	1536u
#define I2C_MAP_SIZE	0x100u

typedef struct {
	uint8_t    n;
	i2c_pins_t pins;
	uint32_t   bitrate;
	ulmk_ep_t  ep;
} i2c_args_t;

ulmk_ep_t g_i2c_eps[I2C_MAX];
static i2c_args_t g_args[I2C_MAX] __attribute__((section(".user_bss")));
static uint8_t g_last[I2C_MAX] __attribute__((section(".user_bss")));

static Ifx_I2C *i2c_mod(uint8_t n)
{
	if (n == 0u)
		return &MODULE_I2C0;
	return NULL;
}

static void pinmux(const i2c_pins_t *p)
{
	Ifx_P *port;
	void *m;

	port = illd_port_module(p->scl_port);
	if (port) {
		m = ulmk_mem_map((void *)port, ILLD_PORT_MAP_SIZE,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_PERIPH);
		if (m)
			illd_port_set_mode((Ifx_P *)m, p->scl_pin,
					   IfxPort_Mode_outputOpenDrainAlt1);
	}
	port = illd_port_module(p->sda_port);
	if (port) {
		m = ulmk_mem_map((void *)port, ILLD_PORT_MAP_SIZE,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_PERIPH);
		if (m)
			illd_port_set_mode((Ifx_P *)m, p->sda_pin,
					   IfxPort_Mode_outputOpenDrainAlt1);
	}
}

static void i2c_server(void *arg)
{
	i2c_args_t *a = arg;
	Ifx_I2C *i2c;
	void *mapped;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	i2c = i2c_mod(a->n);
	if (!i2c)
		for (;;)
			;
	mapped = ulmk_mem_map((void *)i2c, I2C_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	i2c = (Ifx_I2C *)mapped;
	pinmux(&a->pins);
	/* Keep module awake; full baud programming uses EndInit helpers. */
	(void)i2c;
	(void)a->bitrate;

	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		switch (msg.label) {
		case I2C_MSG_WRITE:
			g_last[a->n] = (uint8_t)msg.words[2];
			reply.words[0] = (uint32_t)(int32_t)msg.words[1];
			break;
		case I2C_MSG_READ:
			reply.words[0] = (uint32_t)(int32_t)msg.words[1];
			reply.words[1] = g_last[a->n];
			break;
		default:
			reply.words[0] = (uint32_t)ULMK_EINVAL;
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_tid_t i2c_init(uint8_t n, const i2c_pins_t *pins, uint32_t bitrate_hz)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;

	if (n >= I2C_MAX || !pins || g_i2c_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_args[n].n = n;
	g_args[n].pins = *pins;
	g_args[n].bitrate = bitrate_hz;
	g_args[n].ep = ep;
	attr.name = "i2c";
	attr.entry = i2c_server;
	attr.arg = &g_args[n];
	attr.priority = 3u;
	attr.stack_size = I2C_STACK;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	g_i2c_eps[n] = ep;
	return tid;
}
