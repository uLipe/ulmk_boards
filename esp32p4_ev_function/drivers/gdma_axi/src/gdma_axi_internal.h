/* SPDX-License-Identifier: MIT */
#ifndef GDMA_AXI_INTERNAL_H
#define GDMA_AXI_INTERNAL_H

#include <ulmk/microkernel.h>

#define GDMA_AXI_MAX_INST	1u

enum {
	GDMA_AXI_MSG_OPEN    = 1u,
	GDMA_AXI_MSG_MEMCPY  = 2u,
	GDMA_AXI_MSG_RX_ARM  = 3u,
	GDMA_AXI_MSG_RX_WAIT = 4u,
	GDMA_AXI_MSG_TX_ARM  = 5u,
	GDMA_AXI_MSG_TX_WAIT = 6u,
};

extern ulmk_ep_t g_gdma_axi_eps[GDMA_AXI_MAX_INST];

#endif /* GDMA_AXI_INTERNAL_H */
