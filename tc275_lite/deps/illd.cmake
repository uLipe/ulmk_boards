# SPDX-License-Identifier: MIT
#
# Selective Infineon iLLD (TC27D) integration for tc275_lite.
# Source: deps/illd_tc2x (https://github.com/Infineon/illd_release_tc2x V1.22.0)
#
# Layering:
#   board_init (kernel, pre-.data): header-only WDT inlines + PLL.  No Ifx*.c.
#   driver servers (userspace IO=1): iLLD Std headers + IFX_INLINE APIs as HAL.
#
# Full IfxPort.c / IfxAsclin.c / IfxI2c.c are NOT linked: they call
# IfxScuWdt_* → IfxCpu_getCoreIndex() → mfcr, which traps at ULMK_PRIV_DRIVER.
# CLC enable for peripherals is done in board_init under EndInit instead.
# Pinmux / TX/RX / GPIO use the same register layout via iLLD inlines + SFR.

set(_ILLD_ROOT "${CMAKE_CURRENT_LIST_DIR}/illd_tc2x")
set(_ILLD_BS   "${_ILLD_ROOT}/src/TC27D/BaseSw")
set(_ILLD_TC   "${_ILLD_BS}/iLLD/TC27D/Tricore")

if(NOT EXISTS "${_ILLD_TC}/Scu/Std/IfxScuWdt.h")
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
	"${_ILLD_TC}"
	"${_ILLD_BS}/Infra/Sfr/TC27D/_Reg"
	"${_ILLD_BS}/Infra/Platform"
	"${_ILLD_BS}/Infra/Platform/Tricore/Compilers"
	"${_ILLD_BS}/Service/CpuGeneric"
)

# Optional const pin-map tables (no EndInit / no mfcr).  Safe in userspace.
set(ULMK_BOARD_ILLD_SOURCES "")
foreach(_pm IN ITEMS
	"${_ILLD_TC}/_PinMap/IfxAsclin_PinMap.c"
	"${_ILLD_TC}/_PinMap/IfxI2c_PinMap.c"
	"${_ILLD_TC}/_PinMap/IfxVadc_PinMap.c"
	"${_ILLD_TC}/_PinMap/IfxMultican_PinMap.c"
	"${_ILLD_TC}/_PinMap/IfxGtm_PinMap.c"
	"${_ILLD_TC}/_PinMap/IfxPort_PinMap.c")
	if(EXISTS "${_pm}")
		list(APPEND ULMK_BOARD_ILLD_SOURCES "${_pm}")
	endif()
endforeach()
