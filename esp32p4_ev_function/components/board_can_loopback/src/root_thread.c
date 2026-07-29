/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <can.h>
#include "board_console.h"
#include "board_services.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint32_t id;
	uint8_t rx[8];
	uint8_t len;
	int rc;

	board_services_init(info);
	(void)can_init(0u);
	board_console_puts("CAN loopback (TWAI0 self-test)\r\n");

	for (;;) {
		unsigned i;
		int ok;

		id = 0u;
		len = 0u;
		for (i = 0u; i < 8u; i++)
			rx[i] = 0u;

		rc = can_send(0x100u, payload, 8u);
		(void)can_recv(&id, rx, &len);

		ok = (rc == 0) && (id == 0x100u) && (len == 8u);
		for (i = 0u; ok && i < 8u; i++)
			ok = rx[i] == payload[i];

		board_console_printf("tx id=0x%x rc=%d rx id=0x%x len=%u %s\r\n",
				     0x100u, rc, (unsigned)id, len,
				     ok ? "PASS" : "FAIL");
		board_timer_sleep_us(1000000u);
	}
}
