/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <dma.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

#define XFER_LEN	256u

static uint8_t g_src[XFER_LEN];
static uint8_t g_dst[XFER_LEN];

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	unsigned i;
	int rc;
	int ok;

	board_services_init(info);
	if (dma_init(0u) == ULMK_TID_INVALID ||
	    dma_channel_open(0u, 0u, 0u) != ULMK_OK) {
		board_console_puts("DMA: FAIL init\r\n");
		for (;;)
			board_timer_sleep_us(1000000u);
	}

	board_console_puts("DMA AHB-PDMA mem2mem\r\n");

	for (;;) {
		for (i = 0u; i < XFER_LEN; i++) {
			g_src[i] = (uint8_t)(i ^ 0x5Au);
			g_dst[i] = 0u;
		}

		rc = dma_memcpy(g_dst, g_src, XFER_LEN);
		ok = (rc == ULMK_OK);
		for (i = 0u; ok && i < XFER_LEN; i++)
			ok = g_dst[i] == g_src[i];

		if (ok)
			board_console_printf("DMA: PASS %u bytes\r\n",
					     XFER_LEN);
		else
			board_console_printf(
				"DMA: FAIL rc=%d dst[0]=%02x dst[1]=%02x\r\n",
				rc, g_dst[0], g_dst[1]);
		board_timer_sleep_us(1000000u);
	}
}
