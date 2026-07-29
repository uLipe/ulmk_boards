# LVGL: dual FB + tlsf heap in SDRAM; DIRECT render into LTDC buffers.
set(_LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../deps/lvgl")

ulmk_component_register(
	NAME board_lvgl_benchmark
	ENABLED OFF
	SOURCES
		src/root_thread.c
		src/app_benchmark.c
		src/port_disp.c
		src/port_indev.c
		src/port_tick.c
	ROOT_THREAD
)

if(NOT TARGET ulmk_comp_board_lvgl_benchmark)
	return()
endif()

if(NOT EXISTS "${_LVGL_DIR}/lvgl.h")
	message(FATAL_ERROR "LVGL submodule missing at ${_LVGL_DIR} (tag v9.5.0)")
endif()

file(GLOB_RECURSE _LVGL_SRC CONFIGURE_DEPENDS "${_LVGL_DIR}/src/*.c")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/thorvg/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/ffmpeg/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/libjpeg_turbo/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/libpng/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/lodepng/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/gif/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/qrcode/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/barcode/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/tiny_ttf/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/freetype/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/drivers/.*")

file(GLOB _LVGL_DEMO_SRC CONFIGURE_DEPENDS
	"${_LVGL_DIR}/demos/lv_demos.c"
	"${_LVGL_DIR}/demos/benchmark/*.c"
	"${_LVGL_DIR}/demos/benchmark/assets/*.c"
	"${_LVGL_DIR}/demos/widgets/*.c"
	"${_LVGL_DIR}/demos/widgets/assets/*.c"
)
# BSP overlay: opa card cap + scene logs (keep upstream submodule clean).
list(FILTER _LVGL_DEMO_SRC EXCLUDE REGEX ".*/demos/benchmark/lv_demo_benchmark\\.c$")
list(APPEND _LVGL_DEMO_SRC
	"${CMAKE_CURRENT_LIST_DIR}/demos/lv_demo_benchmark.c")

add_library(ulmk_lvgl STATIC ${_LVGL_SRC})
add_library(ulmk_lvgl_demos STATIC ${_LVGL_DEMO_SRC})

foreach(_tgt ulmk_lvgl ulmk_lvgl_demos)
	target_include_directories(${_tgt} PUBLIC
		"${_LVGL_DIR}"
		# The benchmark overlay lives outside demos/benchmark, so the
		# quoted includes it inherits from upstream need the dir here.
		"${_LVGL_DIR}/demos/benchmark"
		"${CMAKE_CURRENT_LIST_DIR}"
		"${CMAKE_CURRENT_LIST_DIR}/../.."
	)
	target_compile_definitions(${_tgt} PUBLIC
		LV_CONF_INCLUDE_SIMPLE
		LV_LVGL_H_INCLUDE_SIMPLE
	)
	if(DEFINED ULMK_BOARD_CFLAGS)
		target_compile_options(${_tgt} PRIVATE ${ULMK_BOARD_CFLAGS})
	endif()
	# Kernel gets -Ofast via optimize.cmake; this STATIC lib does not.
	# Without it the benchmark warns "Compiler optimization is not enabled"
	# and SW render stays ~5x slower than expected.
	target_compile_options(${_tgt} PRIVATE
		-Ofast
		-Wno-unused-parameter
		-Wno-empty-body
		-Wno-missing-field-initializers
	)
endforeach()

target_link_libraries(ulmk_lvgl_demos PUBLIC ulmk_lvgl)

target_include_directories(ulmk_comp_board_lvgl_benchmark PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/src"
	"${_LVGL_DIR}"
	"${_LVGL_DIR}/demos"
	"${CMAKE_CURRENT_LIST_DIR}"
)
target_compile_definitions(ulmk_comp_board_lvgl_benchmark PRIVATE
	LV_CONF_INCLUDE_SIMPLE
	LV_LVGL_H_INCLUDE_SIMPLE
)
target_compile_options(ulmk_comp_board_lvgl_benchmark PRIVATE -Ofast)
target_link_libraries(ulmk_comp_board_lvgl_benchmark PUBLIC
	ulmk_lvgl
	ulmk_lvgl_demos
)
