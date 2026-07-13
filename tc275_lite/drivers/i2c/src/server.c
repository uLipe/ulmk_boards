/* SPDX-License-Identifier: MIT */
/*
 * i2c server — I2C master via iLLD SFR inlines (CLC/FDIV in board_init).
 *
 * Self-contained: only the I2C module + its protocol SRC.  Completion is
 * IRQ-driven (SRC_I2C0P → notif).  No STM / no transfer-timeout policy —
 * that belongs in the application (e.g. board_i2c_scanner).
 */
#include <ulmk/microkernel.h>
#include <stdint.h>
#include <stddef.h>
#include "i2c_internal.h"
#include "board_config.h"
#include "pinmux_internal.h"
#include "IfxI2c_reg.h"
#include "I2c/Std/IfxI2c.h"

#define I2C_STACK		1536u
#define I2C_XFER_MAX		8u
#define I2C_MAP_SIZE		0x100u
#define I2C_NOTIF_PROTO		0u
#define I2C_SRPN		ULMK_BOARD_IRQ_I2C0_P
#define I2C_SRC			ULMK_BOARD_SRC_I2C0_P

typedef struct {
	uint8_t    n;
	uint8_t    scl_port;
	uint8_t    scl_pin;
	uint8_t    scl_alt;
	uint8_t    sda_port;
	uint8_t    sda_pin;
	uint8_t    sda_alt;
	uint32_t   bitrate;
	ulmk_ep_t  ep;
} i2c_args_t;

ulmk_ep_t g_i2c_eps[I2C_MAX];
static i2c_args_t g_args[I2C_MAX] __attribute__((section(".user_bss")));
static Ifx_I2C *g_i2c __attribute__((section(".user_bss")));
static ulmk_notif_t g_irq_notif __attribute__((section(".user_bss")));

static void pinmux_i2c(const i2c_args_t *a)
{
	pinmux_cfg_t cfg;

	cfg.port  = a->scl_port;
	cfg.pin   = a->scl_pin;
	cfg.dir   = PINMUX_DIR_OUT;
	cfg.pull  = PINMUX_PULL_NONE;
	cfg.alt   = a->scl_alt;
	cfg.flags = PINMUX_F_OPENDRAIN;
	(void)pinmux_apply(&cfg);

	cfg.port  = a->sda_port;
	cfg.pin   = a->sda_pin;
	cfg.alt   = a->sda_alt;
	(void)pinmux_apply(&cfg);
}

static int wait_protocol(Ifx_I2C *i2c)
{
	uint32_t pirq;
	uint32_t bits;
	int ret;

	pirq = i2c->PIRQSS.U;
	if (pirq & ((1u << IFX_I2C_PIRQSS_TX_END_OFF) |
		    (1u << IFX_I2C_PIRQSS_NACK_OFF) |
		    (1u << IFX_I2C_PIRQSS_AL_OFF))) {
		ulmk_irq_ack(I2C_SRPN);
		return ULMK_OK;
	}

	ulmk_irq_ack(I2C_SRPN);
	bits = 0u;
	ret = ulmk_notif_wait(g_irq_notif, 1u << I2C_NOTIF_PROTO, &bits);
	ulmk_irq_ack(I2C_SRPN);
	return ret;
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
			return ULMK_EDEADLK;
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
	int rc;

	if (len > I2C_XFER_MAX)
		return ULMK_EINVAL;
	if (IfxI2c_busIsFree(i2c) == FALSE) {
		release_bus(i2c);
		if (IfxI2c_busIsFree(i2c) == FALSE)
			return ULMK_EDEADLK;
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

		rc = wait_protocol(i2c);
		if (rc != ULMK_OK) {
			release_bus(i2c);
			return rc;
		}
		IfxI2c_clearProtocolInterruptSource(i2c,
			IfxI2c_ProtocolInterruptSource_transmissionEnd);

		while (got < len) {
			uint32_t word;
			uint32_t k;

			if (i2c->FFSSTAT.B.FFS == 0u)
				break;
			word = i2c->RXD.U;
			for (k = 0u; k < 4u && got < len; k++)
				data[got++] = ((uint8_t *)&word)[k];
		}
		if (got < len) {
			release_bus(i2c);
			return ULMK_EPERM;
		}
	}

	release_bus(i2c);
	return (int)len;
}

static int hw_start(void)
{
	Ifx_I2C *i2c = g_i2c;

	IfxI2c_stop(i2c);
	i2c->ADDRCFG.U = 0u;
	i2c->ADDRCFG.B.MnS = 1u;
	i2c->ADDRCFG.B.SONA = 1u;
	i2c->ADDRCFG.B.SOPE = 1u;
	i2c->ADDRCFG.B.TBAM = 0u;
	/*
	 * TXFC=0: a TXD write must not stall the CPU if SCL never toggles.
	 * Protocol IRQ (not DTR polling) completes each phase.
	 */
	i2c->FIFOCFG.U = 0u;
	i2c->ERRIRQSM.U = 0u;
	IfxI2c_enableProtocolInterruptSource(i2c,
		IfxI2c_ProtocolInterruptSource_arbitrationLost);
	IfxI2c_enableProtocolInterruptSource(i2c,
		IfxI2c_ProtocolInterruptSource_notAcknowledgeReceived);
	IfxI2c_enableProtocolInterruptSource(i2c,
		IfxI2c_ProtocolInterruptSource_transmissionEnd);
	IfxI2c_enableProtocolInterruptFlag(i2c);
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
	void *mapped;

	mapped = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_I2C0_BASE,
			      I2C_MAP_SIZE,
			      ULMK_PERM_READ | ULMK_PERM_WRITE,
			      ULMK_MMAP_PERIPH);
	if (!mapped)
		for (;;)
			;
	g_i2c = (Ifx_I2C *)mapped;
	pinmux_i2c(a);
	if (hw_start() != ULMK_OK)
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

ulmk_tid_t i2c_init(uint8_t n, uint32_t bitrate_hz)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_ep_t ep;
	ulmk_tid_t tid;
	int ret;

	if (n >= I2C_MAX || g_i2c_eps[n] != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	if (n != 0u)
		return ULMK_TID_INVALID;

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;

	g_irq_notif = ulmk_notif_create();
	if (g_irq_notif == ULMK_NOTIF_INVALID) {
		ulmk_ep_destroy(ep);
		return ULMK_TID_INVALID;
	}

	ret = ulmk_irq_bind_hw(I2C_SRPN, g_irq_notif, I2C_NOTIF_PROTO,
			       (uintptr_t)I2C_SRC);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;
	ret = ulmk_irq_enable(I2C_SRPN);
	if (ret != ULMK_OK)
		return ULMK_TID_INVALID;

	g_args[n].n = n;
	g_args[n].scl_port = ULMK_BOARD_I2C0_SCL_PORT;
	g_args[n].scl_pin = ULMK_BOARD_I2C0_SCL_PIN;
	g_args[n].scl_alt = ULMK_BOARD_I2C0_SCL_ALT;
	g_args[n].sda_port = ULMK_BOARD_I2C0_SDA_PORT;
	g_args[n].sda_pin = ULMK_BOARD_I2C0_SDA_PIN;
	g_args[n].sda_alt = ULMK_BOARD_I2C0_SDA_ALT;
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
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	g_i2c_eps[n] = ep;
	return tid;
}
