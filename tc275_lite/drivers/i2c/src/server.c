/* SPDX-License-Identifier: MIT */
/*
 * i2c server — I2C0 master via iLLD SFR inlines (CLC/CLC1/FDIV in board_init).
 *
 * Probe = write with len 0 (address byte only), as in iLLD ACK polling.
 * Wall-clock wait uses a volatile spin (no board_timer dependency): floating
 * SDA/SCL without pull-ups must not hang the server forever.
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "i2c_internal.h"
#include "board_config.h"
#include "drivers/common/illd_port.h"
#include "IfxI2c_reg.h"
#include "I2c/Std/IfxI2c.h"

#define I2C_STACK		1536u
#define I2C_XFER_MAX		8u
/* ~few ms at 200 MHz; 127 probes must finish well under HIL timeout. */
#define I2C_SPIN_MAX		200000u

typedef struct {
	uint8_t    n;
	i2c_pins_t pins;
	uint32_t   bitrate;
	ulmk_ep_t  ep;
} i2c_args_t;

ulmk_ep_t g_i2c_eps[I2C_MAX];
static i2c_args_t g_args[I2C_MAX] __attribute__((section(".user_bss")));
static Ifx_I2C *g_i2c __attribute__((section(".user_bss")));

static IfxPort_Mode od_alt(uint8_t alt)
{
	switch (alt) {
	case 5u:
		return IfxPort_Mode_outputOpenDrainAlt5;
	case 6u:
	default:
		return IfxPort_Mode_outputOpenDrainAlt6;
	}
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
					   od_alt(p->scl_alt));
	}
	port = illd_port_module(p->sda_port);
	if (port) {
		m = ulmk_mem_map((void *)port, ILLD_PORT_MAP_SIZE,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_PERIPH);
		if (m)
			illd_port_set_mode((Ifx_P *)m, p->sda_pin,
					   od_alt(p->sda_alt));
	}
}

static int wait_protocol(Ifx_I2C *i2c)
{
	volatile uint32_t spins = 0u;
	uint32_t pirq;

	for (;;) {
		pirq = i2c->PIRQSS.U;
		if (pirq & ((1u << IFX_I2C_PIRQSS_TX_END_OFF) |
			    (1u << IFX_I2C_PIRQSS_NACK_OFF) |
			    (1u << IFX_I2C_PIRQSS_AL_OFF)))
			return ULMK_OK;
		spins++;
		if (spins > I2C_SPIN_MAX)
			return ULMK_ETIMEOUT;
	}
}

static void release_bus(Ifx_I2C *i2c)
{
	if (IfxI2c_getBusStatus(i2c) == IfxI2c_BusStatus_idle)
		return;
	i2c->ENDDCTRL.B.SETEND = 1u;
	(void)wait_protocol(i2c);
	IfxI2c_clearAllProtocolInterruptSources(i2c);
}

static int master_write(Ifx_I2C *i2c, uint8_t addr8, const uint8_t *data,
			size_t len)
{
	int rc;

	if (len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	if (IfxI2c_busIsFree(i2c) == FALSE) {
		release_bus(i2c);
		if (IfxI2c_busIsFree(i2c) == FALSE)
			return ULMK_ETIMEOUT;
	}

	IfxI2c_clearAllProtocolInterruptSources(i2c);
	IfxI2c_clearAllErrorInterruptSources(i2c);
	IfxI2c_setTransmitPacketSize(i2c, 1);
	IfxI2c_writeFifo(i2c, addr8);
	IfxI2c_clearLastSingleRequestInterruptSource(i2c);
	IfxI2c_clearSingleRequestInterruptSource(i2c);
	IfxI2c_clearLastBurstRequestInterruptSource(i2c);
	IfxI2c_clearBurstRequestInterruptSource(i2c);

	rc = wait_protocol(i2c);
	if (rc != ULMK_OK) {
		release_bus(i2c);
		IfxI2c_stop(i2c);
		IfxI2c_run(i2c);
		return rc;
	}

	if (IfxI2c_getProtocolInterruptSourceStatus(i2c,
	    IfxI2c_ProtocolInterruptSource_arbitrationLost) == TRUE) {
		IfxI2c_clearAllProtocolInterruptSources(i2c);
		release_bus(i2c);
		return ULMK_EPERM;
	}
	if (IfxI2c_getProtocolInterruptSourceStatus(i2c,
	    IfxI2c_ProtocolInterruptSource_notAcknowledgeReceived) == TRUE) {
		IfxI2c_clearAllProtocolInterruptSources(i2c);
		release_bus(i2c);
		return ULMK_ESRCH;
	}
	IfxI2c_clearProtocolInterruptSource(i2c,
		IfxI2c_ProtocolInterruptSource_transmissionEnd);

	if (len > 0u) {
		uint32_t i;
		uint32_t packet;
		uint32_t bytes;
		uint32_t remain = (uint32_t)len + 1u;

		IfxI2c_setTransmitPacketSize(i2c, (Ifx_SizeT)(len + 1u));
		for (i = 0u; i < len + 1u; i += 4u) {
			uint32_t j;

			bytes = (remain >= 4u) ? 4u : remain;
			remain -= bytes;
			packet = 0u;
			for (j = 0u; j < bytes; j++) {
				if (i == 0u && j == 0u)
					((uint8_t *)&packet)[j] = addr8;
				else
					((uint8_t *)&packet)[j] =
						data[i + j - 1u];
			}
			IfxI2c_writeFifo(i2c, packet);
			IfxI2c_clearLastSingleRequestInterruptSource(i2c);
			IfxI2c_clearSingleRequestInterruptSource(i2c);
			IfxI2c_clearLastBurstRequestInterruptSource(i2c);
			IfxI2c_clearBurstRequestInterruptSource(i2c);
		}
		rc = wait_protocol(i2c);
		if (rc != ULMK_OK) {
			release_bus(i2c);
			IfxI2c_stop(i2c);
			IfxI2c_run(i2c);
			return rc;
		}
		if (IfxI2c_getProtocolInterruptSourceStatus(i2c,
		    IfxI2c_ProtocolInterruptSource_notAcknowledgeReceived)) {
			IfxI2c_clearAllProtocolInterruptSources(i2c);
			release_bus(i2c);
			return ULMK_ESRCH;
		}
		IfxI2c_clearProtocolInterruptSource(i2c,
			IfxI2c_ProtocolInterruptSource_transmissionEnd);
	}

	release_bus(i2c);
	return (int)len;
}

static int master_read(Ifx_I2C *i2c, uint8_t addr8, uint8_t *data, size_t len)
{
	uint32_t packet;
	volatile uint32_t spins;
	int rc;

	if (len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	if (IfxI2c_busIsFree(i2c) == FALSE) {
		release_bus(i2c);
		if (IfxI2c_busIsFree(i2c) == FALSE)
			return ULMK_ETIMEOUT;
	}

	IfxI2c_clearAllProtocolInterruptSources(i2c);
	IfxI2c_clearAllErrorInterruptSources(i2c);
	packet = (uint32_t)addr8 | 1u;
	IfxI2c_setTransmitPacketSize(i2c, 1);
	IfxI2c_setReceivePacketSize(i2c, (Ifx_SizeT)len);
	IfxI2c_writeFifo(i2c, packet);
	IfxI2c_clearLastSingleRequestInterruptSource(i2c);
	IfxI2c_clearSingleRequestInterruptSource(i2c);
	IfxI2c_clearLastBurstRequestInterruptSource(i2c);
	IfxI2c_clearBurstRequestInterruptSource(i2c);

	rc = wait_protocol(i2c);
	if (rc != ULMK_OK) {
		release_bus(i2c);
		IfxI2c_stop(i2c);
		IfxI2c_run(i2c);
		return rc;
	}
	IfxI2c_clearAllDtrInterruptSources(i2c);

	if (IfxI2c_getProtocolInterruptSourceStatus(i2c,
	    IfxI2c_ProtocolInterruptSource_notAcknowledgeReceived) == TRUE) {
		IfxI2c_clearAllProtocolInterruptSources(i2c);
		release_bus(i2c);
		return ULMK_ESRCH;
	}
	if (IfxI2c_getProtocolInterruptSourceStatus(i2c,
	    IfxI2c_ProtocolInterruptSource_arbitrationLost) == TRUE) {
		IfxI2c_clearAllProtocolInterruptSources(i2c);
		release_bus(i2c);
		return ULMK_EPERM;
	}

	if (len > 0u) {
		uint32_t got = 0u;

		spins = 0u;
		while (got < len) {
			uint32_t word;
			uint32_t k;

			if (i2c->FFSSTAT.B.FFS == 0u) {
				spins++;
				if (spins > I2C_SPIN_MAX) {
					release_bus(i2c);
					return ULMK_ETIMEOUT;
				}
				continue;
			}
			spins = 0u;
			word = i2c->RXD.U;
			for (k = 0u; k < 4u && got < len; k++)
				data[got++] = ((uint8_t *)&word)[k];
		}
		rc = wait_protocol(i2c);
		if (rc != ULMK_OK) {
			release_bus(i2c);
			return rc;
		}
		IfxI2c_clearProtocolInterruptSource(i2c,
			IfxI2c_ProtocolInterruptSource_transmissionEnd);
	}

	release_bus(i2c);
	return (int)len;
}

static int hw_start(const i2c_pins_t *pins, uint8_t pisel)
{
	Ifx_I2C *i2c = g_i2c;

	(void)pins;
	(void)pisel; /* PISEL set once in ulmk_board_init() — GPCTL unsafe in IO=1 */
	IfxI2c_stop(i2c);
	i2c->ADDRCFG.U = 0u;
	i2c->ADDRCFG.B.MnS = 1u;
	i2c->ADDRCFG.B.SONA = 1u;
	i2c->ADDRCFG.B.SOPE = 1u;
	i2c->ADDRCFG.B.TBAM = 0u;
	/*
	 * Polled master: TXFC=0.  With flow control on, a TXD write stalls the
	 * CPU until the FIFO drains — fatal if SCL never toggles.
	 */
	i2c->FIFOCFG.U = 0u;
	i2c->ERRIRQSM.U = 0u;
	i2c->PIRQSM.U = 0u;
	i2c->IMSC.U = 0u;
	IfxI2c_run(i2c);
	return ULMK_OK;
}

static void unpack_data(const ulmk_msg_t *msg, uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0u; i < len && i < 4u; i++)
		buf[i] = (uint8_t)((msg->words[2] >> (i * 8u)) & 0xFFu);
	for (; i < len && i < 8u; i++)
		buf[i] = (uint8_t)((msg->words[3] >> ((i - 4u) * 8u)) & 0xFFu);
}

static void pack_data(ulmk_msg_t *reply, const uint8_t *buf, size_t len)
{
	size_t i;

	reply->words[1] = 0u;
	reply->words[2] = 0u;
	for (i = 0u; i < len && i < 4u; i++)
		reply->words[1] |= (uint32_t)buf[i] << (i * 8u);
	for (; i < len && i < 8u; i++)
		reply->words[2] |= (uint32_t)buf[i] << ((i - 4u) * 8u);
}

static void i2c_server(void *arg)
{
	i2c_args_t *a = arg;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint8_t buf[I2C_XFER_MAX];
	uint8_t addr8;
	size_t len;
	int rc;

	/*
	 * Absolute MODULE_I2C0 (static DPR 3 covers flash+MMIO).  Do not write
	 * GPCTL here — that register stalls SPB from IO=1; PISEL is set in
	 * ulmk_board_init().
	 */
	g_i2c = &MODULE_I2C0;
	pinmux(&a->pins);
	if (hw_start(&a->pins, a->pins.pisel) != ULMK_OK)
		for (;;)
			;

	for (;;) {
		if (ulmk_ep_recv(a->ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_OK;
		reply.words[1] = 0u;
		reply.words[2] = 0u;
		reply.words[3] = 0u;

		addr8 = (uint8_t)((msg.words[0] & 0x7Fu) << 1);
		len = (size_t)msg.words[1];

		if (msg.label == I2C_MSG_WRITE) {
			unpack_data(&msg, buf, len);
			rc = master_write(g_i2c, addr8, buf, len);
			reply.words[0] = (uint32_t)(int32_t)rc;
		} else if (msg.label == I2C_MSG_READ) {
			rc = master_read(g_i2c, addr8, buf, len);
			reply.words[0] = (uint32_t)(int32_t)rc;
			if (rc > 0)
				pack_data(&reply, buf, (size_t)rc);
		} else {
			reply.words[0] = (uint32_t)(int32_t)ULMK_EINVAL;
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
	if (n != 0u)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_args[n].n = n;
	g_args[n].pins = *pins;
	g_args[n].bitrate = bitrate_hz ? bitrate_hz : 100000u;
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
	g_i2c_eps[n] = ep;
	return tid;
}
