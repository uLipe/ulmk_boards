# SPDX-License-Identifier: MIT
#
# launchxl_f29h85x/board.cmake — TI LAUNCHXL-F29H85X (F29H850TU9)

set(UL_BOARD_ARCH "c29")
set(ULMK_BOARD_CPU "c29.c0")

# Errata SPRZ569E: Data Line Buffer stale reads under SMP.
set(ULMK_BOARD_C29_DLB_WORKAROUND 1)

set(ULMK_BOARD_SOURCES
	board_init.c
	board_services.c
	board_console.c
	board_leds.c
	board_timer.c
)

# CRC-valid SSUMODE2 SECCFG blobs (NonMain).  Linked only in the flash
# profile; package-seccfg strips them unless ULMK_C29_SECCFG_COMMIT=1 so
# ordinary Main-flash HIL never erases NonMain.
if(ULMK_C29_FLASH)
	list(APPEND ULMK_BOARD_SOURCES
		seccfg/seccfg_cpu1.c
		seccfg/seccfg_cpu2.c
		seccfg/seccfg_cpu3.c
		seccfg/seccfg_cpu4.c)
endif()

set(ULMK_BOARD_INCLUDES
	"${CMAKE_CURRENT_LIST_DIR}"
)

# No QEMU machine — silicon HIL only.
set(UL_BOARD_HIL_SERIAL_HINT "CL850001-if00")
set(UL_BOARD_HIL_CCXML "${CMAKE_CURRENT_LIST_DIR}/targetconfigs/F29H850TU9.ccxml")
