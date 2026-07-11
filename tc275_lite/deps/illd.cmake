# SPDX-License-Identifier: MIT
#
# Selective Infineon iLLD (TC27D) integration for tc275_lite.
# Source: deps/illd_tc2x (https://github.com/Infineon/illd_release_tc2x V1.22.0)
#
# Only headers + WDT inlines are consumed by board_init today.  IfxScuCcu.c
# is NOT linked: it has .data/.bss and cannot run before .data relocation.

set(_ILLD_ROOT "${CMAKE_CURRENT_LIST_DIR}/illd_tc2x")
set(_ILLD_BS   "${_ILLD_ROOT}/src/TC27D/BaseSw")

if(NOT EXISTS "${_ILLD_BS}/iLLD/TC27D/Tricore/Scu/Std/IfxScuWdt.h")
	message(FATAL_ERROR
		"iLLD TC27D missing under ${_ILLD_ROOT}.\n"
		"  cd ${CMAKE_CURRENT_LIST_DIR} && "
		"git clone --depth 1 --branch V1.22.0 "
		"https://github.com/Infineon/illd_release_tc2x.git illd_tc2x\n"
		"  then: git -C illd_tc2x sparse-checkout set src/TC27D "
		"examples/BaseFramework_TC27D/0_Src/AppSw/CpuGeneric/Config "
		"IFASLL202501.pdf README.md")
endif()

set(ULMK_BOARD_ILLD_INCLUDES
	"${CMAKE_CURRENT_LIST_DIR}"
	"${_ILLD_BS}/iLLD/TC27D/Tricore"
	"${_ILLD_BS}/Infra/Sfr/TC27D/_Reg"
	"${_ILLD_BS}/Infra/Platform"
	"${_ILLD_BS}/Infra/Platform/Tricore/Compilers"
)

# No iLLD .c objects for early bring-up (header-only WDT + board PLL).
set(ULMK_BOARD_ILLD_SOURCES "")
