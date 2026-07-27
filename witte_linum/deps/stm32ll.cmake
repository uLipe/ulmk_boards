# SPDX-License-Identifier: MIT
#
# Selective STM32H7 Cube LL (header-only) for witte_linum.
# Source: deps/{cmsis_device_h7,cmsis_core,stm32h7xx_hal_driver}
#
# Layering:
#   board_init (kernel, pre-.data): LL inlines for PLL / RCC / early USART.
#   driver servers (userspace): LL GPIO/USART inlines after ulmk_mem_map.
#
# HAL *.c and HAL_* APIs are intentionally NOT linked.

set(_STM32_DEPS "${CMAKE_CURRENT_LIST_DIR}")
set(_CMSIS_DEV  "${_STM32_DEPS}/cmsis_device_h7")
set(_CMSIS_CORE "${_STM32_DEPS}/cmsis_core")
set(_HAL_DRV    "${_STM32_DEPS}/stm32h7xx_hal_driver")

if(NOT EXISTS "${_CMSIS_DEV}/Include/stm32h753xx.h")
	message(FATAL_ERROR
		"STM32 CMSIS device missing under ${_CMSIS_DEV}.\n"
		"  cd ${_STM32_DEPS} && ./fetch.sh")
endif()
if(NOT EXISTS "${_CMSIS_CORE}/Core/Include/core_cm7.h")
	message(FATAL_ERROR
		"CMSIS Core missing under ${_CMSIS_CORE}.\n"
		"  cd ${_STM32_DEPS} && ./fetch.sh")
endif()
if(NOT EXISTS "${_HAL_DRV}/Inc/stm32h7xx_ll_gpio.h")
	message(FATAL_ERROR
		"STM32H7 LL headers missing under ${_HAL_DRV}.\n"
		"  cd ${_STM32_DEPS} && ./fetch.sh")
endif()

set(ULMK_BOARD_STM32_INCLUDES
	"${_STM32_DEPS}"
	"${_CMSIS_DEV}/Include"
	"${_CMSIS_CORE}/Core/Include"
	"${_HAL_DRV}/Inc"
)

# Header-only — no ST translation units.
set(ULMK_BOARD_STM32_SOURCES "")
