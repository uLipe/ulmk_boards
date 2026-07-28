/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <qspi.h>
#include "board_console.h"
#include "board_services.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint8_t id[3];
	uint32_t sr;
	uint32_t cr;
	int ret;

	board_services_init(info);
	board_console_puts("\r\nqspi jedec\r\n");

	if (qspi_init(0u) == ULMK_TID_INVALID) {
		board_console_puts("qspi init failed\r\n");
		ulmk_thread_exit();
	}

	ret = qspi_cmd_read_dbg(0u, 0x9Fu, id, 3u, &sr);
	cr = qspi_last_cr();
	if (ret != ULMK_OK) {
		board_console_printf("qspi jedec failed %d sr=%08x cr=%08x\r\n",
				     ret, sr, cr);
		ulmk_thread_exit();
	}
	board_console_printf("QSPI JEDEC: %02x %02x %02x\r\n",
			     id[0], id[1], id[2]);
	/*
	 * Transfer-complete path is up when CR.EN stays set after the xfer.
	 * Winbond/Macronix/GigaDevice IDs → full PASS; else TC OK (check NOR).
	 */
	if (id[0] == 0xEFu || id[0] == 0xC2u || id[0] == 0xC8u)
		board_console_puts("QSPI: PASS\r\n");
	else if ((cr & 1u) != 0u)
		board_console_puts("QSPI: TC OK\r\n");
	else
		board_console_puts("QSPI: FAIL\r\n");

	for (;;)
		ulmk_thread_yield();
}
