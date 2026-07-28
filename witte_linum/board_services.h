/* SPDX-License-Identifier: MIT */
#ifndef BOARD_SERVICES_H
#define BOARD_SERVICES_H

#include <ulmk/microkernel.h>
#include "board_cache.h"

void board_services_init(const ulmk_boot_info_t *info);

#endif /* BOARD_SERVICES_H */
