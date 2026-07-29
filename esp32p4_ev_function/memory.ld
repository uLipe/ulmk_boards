/*
 * esp32p4_ev_function/memory.ld — Layer 3 chip input (ESP32-P4).
 *
 * Flash XIP at 0x40000020 (0x20 offset for MMU page alignment w/ image hdr).
 * HP SRAM after typical 128 KiB L2: 0x4FF20000 .. 0x4FFAEFC0.
 * LP SRAM as SHARED for dual-core / IPC-friendly window.
 */
MEMORY {
	KERNEL_FLASH  (rx)  : ORIGIN = 0x40000020, LENGTH = 2048K
	KERNEL_RAM    (rwx) : ORIGIN = 0x4FF20000, LENGTH = 0x8EFC0
	SHARED_RAM    (rwx) : ORIGIN = 0x50108000, LENGTH = 32K
	PERIPH        (rw)  : ORIGIN = 0x50000000, LENGTH = 2M
}

ULMK_KERNEL_STACK_SIZE = 0x2000;
ULMK_ISR_STACK_SIZE    = 0x1000;
ULMK_MPU_ALIGN         = 128;

HAVE_CSA        = 0;
HAVE_SMALL_DATA = 0;
HAVE_BMHD       = 0;
