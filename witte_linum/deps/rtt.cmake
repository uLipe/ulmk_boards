# SPDX-License-Identifier: MIT
#
# SEGGER RTT (header + SEGGER_RTT.c) for witte_linum console.

set(_RTT_ROOT "${CMAKE_CURRENT_LIST_DIR}/rtt")
set(_RTT_SRC  "${_RTT_ROOT}/RTT")

if(NOT EXISTS "${_RTT_SRC}/SEGGER_RTT.c")
	message(FATAL_ERROR
		"SEGGER RTT missing under ${_RTT_ROOT}.\n"
		"  cd ${CMAKE_CURRENT_LIST_DIR} && ./fetch.sh")
endif()

list(APPEND ULMK_BOARD_STM32_INCLUDES
	"${CMAKE_CURRENT_LIST_DIR}"
	"${_RTT_SRC}"
	"${_RTT_ROOT}/Config"
)

list(APPEND ULMK_BOARD_STM32_SOURCES
	"${_RTT_SRC}/SEGGER_RTT.c"
)
