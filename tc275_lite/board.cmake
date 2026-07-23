# SPDX-License-Identifier: MIT
#
# tc275_lite/board.cmake — Infineon AURIX TC275 Lite Kit (KIT_AURIX_TC275_LITE).

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

include("${CMAKE_CURRENT_LIST_DIR}/deps/illd.cmake")

set(ULMK_BOARD_SOURCES
    bmhd.c
    board_wdt_early.S
    board_init.c
    board_hil.c
    board_printk_stub.c
    board_services.c
    board_console.c
    board_timer.c
    board_leds.c
    drivers/pinmux/src/server.c
    drivers/pinmux/src/client.c
    drivers/gpio/src/server.c
    drivers/gpio/src/client.c
    drivers/asclin/src/server.c
    drivers/asclin/src/client.c
    drivers/i2c/src/server.c
    drivers/i2c/src/client.c
    drivers/adc/src/server.c
    drivers/adc/src/client.c
    drivers/can/src/server.c
    drivers/can/src/client.c
    drivers/pwm/src/server.c
    drivers/pwm/src/client.c
    ${ULMK_BOARD_ILLD_SOURCES}
)

set(ULMK_BOARD_INCLUDES
    ${ULMK_BOARD_ILLD_INCLUDES}
    "${CMAKE_CURRENT_LIST_DIR}/drivers/pinmux/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/pinmux/src"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/gpio/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/gpio/src"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/asclin/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/asclin/src"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/i2c/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/i2c/src"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/adc/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/adc/src"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/can/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/can/src"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/pwm/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/pwm/src"
    "${CMAKE_CURRENT_LIST_DIR}"
)
