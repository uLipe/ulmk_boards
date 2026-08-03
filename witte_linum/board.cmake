# SPDX-License-Identifier: MIT
#
# witte_linum/board.cmake — Witte Technology Linum (STM32H753ZI).

set(UL_BOARD_ARCH "arm")
set(ULMK_BOARD_CPU "cortex-m7")

# The H753 FPU is double precision (fpv5-d16), which is also what CubeIDE
# emits — matching it keeps SDK consumers from having to retune the project.
# ABI follows ULMK_CONFIG_FPU.
include("${_ULMK_REPO_ROOT}/cmake/arm_fpu.cmake")
ulmk_arm_float_flags(cortex-m7 fpv5-d16)

include("${CMAKE_CURRENT_LIST_DIR}/deps/stm32ll.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/deps/rtt.cmake")

# Device select for stm32h7xx.h / LL headers (also in stm32_conf.h).
set(ULMK_BOARD_CFLAGS "-DSTM32H753xx" "-DRTT_USE_ASM=0")

set(ULMK_BOARD_SOURCES
	board_init.c
	board_system.c
	board_rtt.c
	board_printk.c
	board_services.c
	board_cache.c
	board_console.c
	board_timer.c
	board_leds.c
	board_sdram.c
	board_hil.c
	drivers/pinmux/src/server.c
	drivers/pinmux/src/client.c
	drivers/gpio/src/server.c
	drivers/gpio/src/client.c
	drivers/uart/src/server.c
	drivers/uart/src/client.c
	drivers/pwm/src/server.c
	drivers/pwm/src/client.c
	drivers/can/src/server.c
	drivers/can/src/client.c
	drivers/adc/src/server.c
	drivers/adc/src/client.c
	drivers/dma/src/server.c
	drivers/dma/src/client.c
	drivers/display/src/server.c
	drivers/display/src/client.c
	drivers/display_dm/src/server.c
	drivers/i2c/src/server.c
	drivers/i2c/src/client.c
	drivers/touch/src/server.c
	drivers/touch/src/client.c
	drivers/touch_dm/src/server.c
	drivers/qspi/src/server.c
	drivers/qspi/src/client.c
	${ULMK_BOARD_STM32_SOURCES}
)

set(ULMK_BOARD_INCLUDES
	${ULMK_BOARD_STM32_INCLUDES}
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pinmux/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pinmux/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/gpio/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/gpio/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/uart/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/uart/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pwm/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pwm/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/can/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/can/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/adc/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/adc/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/dma/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/dma/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/display/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/display/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/display_dm/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/i2c/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/i2c/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/touch/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/touch/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/touch_dm/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/qspi/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/qspi/src"
	"${CMAKE_CURRENT_LIST_DIR}"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/components/ulmk_device_manager/include/ulmk_device.h")
	set(ULMK_COMP_ulmk_device_manager_ENABLED ON CACHE BOOL
		"Enable device manager (required by board_devices.c)" FORCE)
	list(APPEND ULMK_BOARD_INCLUDES
		"${CMAKE_SOURCE_DIR}/components/ulmk_device_manager/include")
	list(APPEND ULMK_BOARD_SOURCES board_devices.c)
	set(_ULMK_BOARD_HAS_DEV_MGR ON)
endif()

# App-owned device class contracts — policy in ulmk_apps, not the kernel tree.
if(EXISTS "${CMAKE_SOURCE_DIR}/../ulmk_apps/ulmk_device_classes/include")
	list(APPEND ULMK_BOARD_INCLUDES
		"${CMAKE_SOURCE_DIR}/../ulmk_apps/ulmk_device_classes/include")
endif()
