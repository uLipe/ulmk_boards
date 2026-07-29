# SPDX-License-Identifier: MIT
#
# esp32p4_ev_function/board.cmake — ESP32-P4 Function EV Board (UP).
#
# Toolchain: riscv32-esp-elf from ESP-IDF (~/.espressif/tools).
# HIL/flash: host + IDF export (not QEMU). See scripts/ and README.md.

set(UL_BOARD_ARCH "riscv")
set(ULMK_BOARD_CPU "rv32imafc")
set(UL_BOARD_TOOLCHAIN "riscv-esp32p4")
set(UL_BOARD_HOST_BUILD 1)

# Root thread drives the framebuffer demos; 4 KiB overflows into an
# INST_FAULT once printf varargs and the draw helpers are on the stack.
set(ULMK_CONFIG_ROOT_STACK_SIZE 16384 CACHE STRING
	"Root thread stack size in bytes")

# DW_GDMA→DPI must re-arm the LLI in ISR context or the bridge underruns
# before a worker wakeup; the DSI driver uses ulmk_irq_attach_hw for that.
set(ULMK_CONFIG_IRQ_ATTACH 1 CACHE STRING
	"Allow userspace ISR attach callbacks")

# INTMTX sits between each peripheral and the CLIC, so the kernel has to route
# a line when a driver binds it.
set(ULMK_CONFIG_BOARD_IRQ_CTRL 1 CACHE STRING
	"Route board IRQ lines through the interrupt matrix on bind")
# The tick is board glue and is handled before generic dispatch.
set(ULMK_CONFIG_BOARD_IRQ_CLAIM 1 CACHE STRING
	"Board sees each IRQ before the kernel dispatches it")
# LP peripherals, PSRAM and the non-cacheable SRAM alias need their own entries.
set(ULMK_CONFIG_BOARD_PMP_EXTRA 1 CACHE STRING
	"Board adds its own PMP entries")

set(_ULMK_P4_MFLAGS
	"-march=rv32imafc -mabi=ilp32f -ffunction-sections -fdata-sections")
if(DEFINED CMAKE_C_FLAGS)
	string(APPEND CMAKE_C_FLAGS " ${_ULMK_P4_MFLAGS}")
	string(APPEND CMAKE_ASM_FLAGS " ${_ULMK_P4_MFLAGS}")
	string(APPEND CMAKE_EXE_LINKER_FLAGS " ${_ULMK_P4_MFLAGS}")
endif()

if(NOT DEFINED ESP_IDF_PATH)
	if(DEFINED ENV{ESP_IDF_PATH})
		set(ESP_IDF_PATH "$ENV{ESP_IDF_PATH}")
	elseif(DEFINED ENV{IDF_PATH})
		set(ESP_IDF_PATH "$ENV{IDF_PATH}")
	elseif(EXISTS "/home/ulipe/fun/esp-idf")
		set(ESP_IDF_PATH "/home/ulipe/fun/esp-idf")
	endif()
endif()

if(NOT ESP_IDF_PATH OR NOT EXISTS "${ESP_IDF_PATH}/components/soc/esp32p4")
	message(FATAL_ERROR
		"ESP_IDF_PATH not set or missing esp32p4 SOC headers")
endif()

set(ULMK_BOARD_ESP_IDF_PATH "${ESP_IDF_PATH}" CACHE PATH "ESP-IDF root")

set(ULMK_BOARD_CFLAGS
	"-DESP_PLATFORM"
	"-DCONFIG_IDF_TARGET_ESP32P4=1"
)

set(ULMK_BOARD_INCLUDES
	"${CMAKE_CURRENT_LIST_DIR}"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pinmux/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pinmux/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/gpio/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/gpio/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/uart/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/uart/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pwm/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/pwm/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/i2c/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/i2c/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/adc/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/adc/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/dma/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/dma/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/display/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/display/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/dsi/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/dsi/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/touch/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/touch/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/spi/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/spi/src"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/can/include"
	"${CMAKE_CURRENT_LIST_DIR}/drivers/can/src"
	"${ESP_IDF_PATH}/components/soc/esp32p4/include"
	"${ESP_IDF_PATH}/components/soc/esp32p4/register/hw_ver3"
	"${ESP_IDF_PATH}/components/soc/include"
	"${ESP_IDF_PATH}/components/hal/include"
	"${ESP_IDF_PATH}/components/hal/esp32p4/include"
	"${ESP_IDF_PATH}/components/esp_rom/include"
	"${ESP_IDF_PATH}/components/esp_rom/esp32p4/include"
	"${ESP_IDF_PATH}/components/esp_common/include"
)

set(ULMK_BOARD_SOURCES
	board_init.c
	board_pmp.c
	board_app_desc.c
	board_printk.c
	board_console.c
	board_timer.c
	board_services.c
	board_irq.c
	board_tick.c
	board_psram.c
	board_cache.c
	board_mpll.c
	board_leds.c
	board_lcd.c
	drivers/dsi/src/dsi.c
	drivers/dsi/src/dsi_fb.c
	drivers/pinmux/src/server.c
	drivers/pinmux/src/client.c
	drivers/gpio/src/server.c
	drivers/gpio/src/client.c
	drivers/gpio/src/gpio_hw.c
	drivers/uart/src/server.c
	drivers/uart/src/client.c
	drivers/pwm/src/server.c
	drivers/pwm/src/client.c
	drivers/pwm/src/pwm_hw.c
	drivers/i2c/src/server.c
	drivers/i2c/src/client.c
	drivers/adc/src/server.c
	drivers/adc/src/client.c
	drivers/adc/src/adc_hw.c
	drivers/dma/src/server.c
	drivers/dma/src/client.c
	drivers/display/src/server.c
	drivers/display/src/client.c
	drivers/touch/src/server.c
	drivers/touch/src/client.c
	drivers/spi/src/server.c
	drivers/spi/src/client.c
	drivers/can/src/server.c
	drivers/can/src/client.c
)

# ROM PROVIDE scripts (absolute INCLUDE written for generate_ld.py)
set(_ULMK_IDF_ROM_LD "${ESP_IDF_PATH}/components/esp_rom/esp32p4/ld")
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/rom.ld"
	"INCLUDE \"${CMAKE_CURRENT_LIST_DIR}/periph_syms.ld\"\n"
	"INCLUDE \"${_ULMK_IDF_ROM_LD}/esp32p4.rom.ld\"\n"
	"INCLUDE \"${_ULMK_IDF_ROM_LD}/esp32p4.rom.api.ld\"\n"
	"INCLUDE \"${_ULMK_IDF_ROM_LD}/esp32p4.rom.systimer.ld\"\n"
	"INCLUDE \"${_ULMK_IDF_ROM_LD}/esp32p4.rom.libgcc.ld\"\n"
	"INCLUDE \"${_ULMK_IDF_ROM_LD}/esp32p4.rom.libc.ld\"\n"
	"INCLUDE \"${_ULMK_IDF_ROM_LD}/esp32p4.rom.newlib.ld\"\n"
)
