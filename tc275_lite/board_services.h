/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_services.h — portable board service entry point.
 */

#ifndef BOARD_SERVICES_H
#define BOARD_SERVICES_H

#include <ulmk/microkernel.h>

void board_services_init(const ulmk_boot_info_t *info);

#endif /* BOARD_SERVICES_H */
