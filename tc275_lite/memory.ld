/* tc275_lite/memory.ld — Layer 3 chip input for SAK-TC275TP (3-core SMP).
 * See ulmk/docs/linker_spec.md §9.
 */
MEMORY {
	KERNEL_FLASH_NC (rx)  : ORIGIN = 0x80000000, LENGTH = 4M
	KERNEL_FLASH    (rx)  : ORIGIN = 0xA0000000, LENGTH = 4M
	/*
	 * CPU0 is TC1.6E on TC275: DSPR = 112 KB (0x70000000–0x7001BFFF).
	 * Past 0x7001C000 is reserved (SRIBE → Class 4 bus error).
	 */
	KERNEL_RAM      (rwx) : ORIGIN = 0x70000000, LENGTH = 112K
	/*
	 * CPU1 / CPU2 DSPR: CSA + ISP + park stack only — not user_pool.
	 * Global 0x60000000 / 0x50000000; local alias 0xD0000000 from that core.
	 */
	KERNEL_RAM_CPU1 (rwx) : ORIGIN = 0x60000000, LENGTH = 24K
	KERNEL_RAM_CPU2 (rwx) : ORIGIN = 0x50000000, LENGTH = 24K
	SHARED_RAM      (rwx) : ORIGIN = 0x90000000, LENGTH = 32K
	PERIPH          (rw)  : ORIGIN = 0xF0000000, LENGTH = 256M
}

ULMK_CSA_POOL_SIZE       = 0x4000;
ULMK_CSA_POOL_CPU1_SIZE  = 0x2000;
ULMK_CSA_POOL_CPU2_SIZE  = 0x2000;
ULMK_KERNEL_STACK_SIZE   = 0x1000;
ULMK_ISR_STACK_SIZE      = 0x0800;
ULMK_MPU_ALIGN           = 64;

HAVE_CSA        = 1;
HAVE_CSA_CPU1   = 1;
HAVE_CSA_CPU2   = 1;
HAVE_SMALL_DATA = 1;
HAVE_BMHD       = 1;
