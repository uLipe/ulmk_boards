/* launchxl_f29h85x/memory_flash.cmd — Layer 3 chip MEMORY, FLASH profile.
 *
 * Autonomous flash boot map (TI F29H85x, hw_memmap.h / flash examples):
 *   CERT       0x10000000 4 KiB  — x509 boot certificate (filled post-build)
 *   FLASH_RP0  0x10001000        — CPU1 code/rodata (execute in place)
 *   Secondary stubs append to the same contiguous FLASH_RP0 image as
 *   CPU1 (linker .cpu2_stub/.cpu3_stub) so BANKMODE0 "Necessary Banks
 *   Only" erase covers them — sparse high LMAs / FLASH_RP1 left sectors
 *   unerased ("already programmed") and Entire Flash + free-run stuck
 *   the kit on wr_pll.alg.
 *   LDA        0x200E0000        — CPU1 data/bss (RUN), copied from flash LMA
 *   SHARED_RAM 0x200F8000        — cross-CPU shared window
 *
 * .data RUNs in LDA with its LMA in FLASH_RP0; kernel/init/init.c relocates
 * it at boot (LMA != VMA).  .rodata/.const stay in flash (XIP).
 */

ULMK_KERNEL_STACK_SIZE = 0x1000;
ULMK_ISR_STACK_SIZE    = 0x0800;
ULMK_MPU_ALIGN         = 4096;
ULMK_USER_POOL_SIZE    = 0x4000;

HAVE_CSA        = 0;
HAVE_SMALL_DATA = 0;
HAVE_BMHD       = 0;

MEMORY
{
	/* Boot certificate slot — only the explicit `cert` section may land here. */
	CERT         : origin = 0x10000000, length = 0x1000
	/* CPU1 code + rodata + contiguous secondary stub LMAs */
	KERNEL_FLASH : origin = 0x10001000, length = 0xFF000
	/* NonMain SECCFG (BANKMODE0) — only programmed with SECCFG_COMMIT */
	SECCFG_CPU1  : origin = 0x10D85000, length = 0x800
	SECCFG_CPU2  : origin = 0x10D85800, length = 0x800
	SECCFG_CPU3  : origin = 0x10D8D000, length = 0x800
	SECCFG_CPU4  : origin = 0x10D8D800, length = 0x800
	/* CPU1 data/bss (VMA) + shared LDA window */
	KERNEL_RAM   : origin = 0x200E0000, length = 0x18000
	SHARED_RAM   : origin = 0x200F8000, length = 0x8000
	PERIPH       : origin = 0x30000000, length = 0x10000000
}
