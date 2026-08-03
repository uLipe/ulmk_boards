# SPDX-License-Identifier: MIT
#
# tc275_lite/board.cmake — Infineon AURIX TC275 Lite Kit (KIT_AURIX_TC275_LITE).

set(UL_BOARD_ARCH "tricore")
set(ULMK_BOARD_CPU  "tc27xx")

# Sample kit ships irq_attach demos — default ON here (global default is OFF).
# Override with -DULMK_CONFIG_IRQ_ATTACH=0.  Enabling opens a trusted ISR path
# into userspace; that is an intentional kernel aperture owned by the product.
set(ULMK_CONFIG_IRQ_ATTACH 1 CACHE STRING
	"Enable ulmk_irq_attach (0=off/ENOTSUP, 1=DANGEROUS ISR userspace callbacks)")

if(DEFINED CMAKE_C_FLAGS)
    string(REGEX REPLACE " -mcpu=[^ ]+" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE " -mcpu=[^ ]+" "" CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS}")
    string(REGEX REPLACE " -mcpu=[^ ]+" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(APPEND CMAKE_C_FLAGS          " -mcpu=${ULMK_BOARD_CPU}")
    string(APPEND CMAKE_ASM_FLAGS        " -mcpu=${ULMK_BOARD_CPU}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -mcpu=${ULMK_BOARD_CPU}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/deps/illd.cmake")

if(EXISTS "${CMAKE_SOURCE_DIR}/components/ulmk_device_manager/include/ulmk_device.h")
	set(ULMK_COMP_ulmk_device_manager_ENABLED ON CACHE BOOL
		"Enable device manager (required by board_devices.c)" FORCE)
	list(APPEND ULMK_BOARD_INCLUDES
		"${CMAKE_SOURCE_DIR}/components/ulmk_device_manager/include")
	set(_ULMK_BOARD_HAS_DEV_MGR ON)
endif()

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

if(_ULMK_BOARD_HAS_DEV_MGR)
	list(APPEND ULMK_BOARD_SOURCES
		board_devices.c
		drivers/can_dm/src/server.c
		drivers/pwm_dm/src/server.c
		drivers/adc_dm/src/server.c
		drivers/gpio_dm/src/server.c
	)
endif()

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
    "${CMAKE_CURRENT_LIST_DIR}/drivers/can_dm/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/pwm_dm/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/adc_dm/include"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/gpio_dm/include"
    "${CMAKE_CURRENT_LIST_DIR}"
)

# Re-append device-manager includes after the base list (set() above
# would otherwise drop any early APPEND when CMAKE_SOURCE_DIR is ready).
if(_ULMK_BOARD_HAS_DEV_MGR)
	list(APPEND ULMK_BOARD_INCLUDES
		"${CMAKE_SOURCE_DIR}/components/ulmk_device_manager/include")
	# Class contracts (can/pwm/adc/gpio) live in ulmk_apps policy package.
	if(EXISTS "${CMAKE_SOURCE_DIR}/../ulmk_apps/ulmk_device_classes/include")
		list(APPEND ULMK_BOARD_INCLUDES
			"${CMAKE_SOURCE_DIR}/../ulmk_apps/ulmk_device_classes/include")
	endif()
endif()
