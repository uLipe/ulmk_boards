/* launchxl_f29h85x/memory.cmd — Layer 3 chip MEMORY for TI C29 linker.
 *
 * TI F29H85x RAM program map (hw_memmap.h / multicore LED examples):
 *   LPA0 0x20100000 32 KiB — CPU1 (primary)
 *   LPA1 0x20108000 32 KiB — CPU2 only (CPU2 cannot fetch from CPA)
 *   CPA0 0x20110000 32 KiB — CPU3 only
 *   CPA1 0x20118000 32 KiB — CPU1 overflow (CPU1 may fetch CPA)
 * Shared data lives in LDA (KERNEL_RAM).
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
	/* CPU1 program — LPA0, with CPA1 as overflow when .text > 32 KiB */
	KERNEL_FLASH : origin = 0x20100000, length = 0x8000
	CPU1_CPA1    : origin = 0x20118000, length = 0x8000
	/* CPU1 local data + shared LDA window visible to all CPUs */
	KERNEL_RAM   : origin = 0x200E0000, length = 0x18000
	SHARED_RAM   : origin = 0x200F8000, length = 0x8000
	/* Secondary program banks (TI affinity: CPU2→LPA, CPU3→CPA) */
	CPU2_LPA1    : origin = 0x20108000, length = 0x8000
	CPU3_CPA0    : origin = 0x20110000, length = 0x8000
	PERIPH       : origin = 0x30000000, length = 0x10000000
}
