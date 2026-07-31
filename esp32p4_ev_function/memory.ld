/*
 * esp32p4_ev_function/memory.ld — Layer 3 chip input (ESP32-P4 rev < v3).
 *
 * Flash XIP at 0x40000020 (0x20 offset for MMU page alignment w/ image hdr).
 * LP SRAM as SHARED for dual-core / IPC-friendly window.
 *
 * HP SRAM is not one block on this revision.  The 128 KiB L2 is carved off
 * the bottom, and 0x4FF2CBD0 .. 0x4FF40000 belongs to the ROM: among other
 * things rom_spiflash_legacy_data/_funcs live at 0x4FF3FFE8, and the MSPI
 * routines board_psram_init() calls dereference them.  Covering that hole
 * with .bss zeroes those pointers and faults inside ROM code.
 *
 * That leaves the 51 KiB below the hole usable only for code loaded before
 * the ROM is next called — which is exactly the kernel, so KERNEL_IRAM hands
 * it to .startup/.trap/.kernel_text instead of wasting it.
 */
MEMORY {
	KERNEL_FLASH  (rx)  : ORIGIN = 0x40000020, LENGTH = 2048K
	KERNEL_IRAM   (rwx) : ORIGIN = 0x4FF20000, LENGTH = 0xCBD0
	KERNEL_RAM    (rwx) : ORIGIN = 0x4FF40000, LENGTH = 0x60000
	SHARED_RAM    (rwx) : ORIGIN = 0x50108000, LENGTH = 32K
	PERIPH        (rw)  : ORIGIN = 0x50000000, LENGTH = 2M
}

ULMK_KERNEL_STACK_SIZE = 0x2000;
ULMK_ISR_STACK_SIZE    = 0x1000;
ULMK_MPU_ALIGN         = 128;

HAVE_CSA        = 0;
HAVE_SMALL_DATA = 0;
HAVE_BMHD       = 0;
