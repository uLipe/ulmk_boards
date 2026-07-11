# SPDX-License-Identifier: MIT
#
# tc275_lite/board.cmake — Infineon AURIX TC275 Lite Kit (KIT_AURIX_TC275_LITE).
#
# Point ulmk at this directory:
#   cmake -DULMK_CHIP_DIR=/path/to/ulmk_boards/tc275_lite

set(UL_BOARD_ARCH "tricore")
set(ULMK_BOARD_CPU  "tc27xx")

if(DEFINED CMAKE_C_FLAGS)
    string(REGEX REPLACE " -mcpu=[^ ]+" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE " -mcpu=[^ ]+" "" CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}")
    string(REGEX REPLACE " -mcpu=[^ ]+" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(APPEND CMAKE_C_FLAGS          " -mcpu=${ULMK_BOARD_CPU}")
    string(APPEND CMAKE_ASM_FLAGS        " -mcpu=${ULMK_BOARD_CPU}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -mcpu=${ULMK_BOARD_CPU}")
endif()

# Infineon iLLD TC27D (WDT EndInit inlines + SFR headers for PLL).
include("${CMAKE_CURRENT_LIST_DIR}/deps/illd.cmake")

set(ULMK_BOARD_SOURCES
    bmhd.c
    board_init.c
    board_hil.c
    board_printk_stub.c
    board_services.c
    board_console.c
    board_timer.c
    board_gpio.c
    board_leds.c
    board_adc.c
    board_i2c.c
    board_can.c
    board_pwm.c
    drivers/asclin/asclin_uart.c
    drivers/port/port14_asclin0.c
    ${ULMK_BOARD_ILLD_SOURCES}
)

set(ULMK_BOARD_INCLUDES ${ULMK_BOARD_ILLD_INCLUDES})
