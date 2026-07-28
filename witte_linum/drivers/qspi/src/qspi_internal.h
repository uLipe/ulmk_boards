/* SPDX-License-Identifier: MIT */
#ifndef QSPI_INTERNAL_H
#define QSPI_INTERNAL_H

#include <ulmk/microkernel.h>
#include <qspi.h>

#define QSPI_MSG_CMD_READ	1u
#define QSPI_MSG_READ		2u
#define QSPI_XFER_MAX		64u
#define QSPI_NOTIF_TC		0u

extern ulmk_ep_t g_qspi_ep;

#endif /* QSPI_INTERNAL_H */
