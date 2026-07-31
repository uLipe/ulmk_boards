/* SPDX-License-Identifier: MIT */
#ifndef BOARD_PSRAM_TUNE_H
#define BOARD_PSRAM_TUNE_H

/*
 * Run after mode regs + probe.  board_psram_enable_axi() calls this after
 * config_axi() and before the MMU map — same order as IDF.
 * Returns 0 on verify-ok, -1 if the chosen point still mismatches.
 */
int board_psram_timing_tune(void);

/* Re-apply the last tuned phase/delayline (e.g. after mode-reg churn). */
void board_psram_timing_apply(void);

/*
 * Second pass over the delayline table using the AXI + D-cache path
 * (write, clean, invalidate, read).  Call after the MMU window is mapped.
 */
int board_psram_timing_refine_axi(void *base, uint32_t nbytes);

#endif /* BOARD_PSRAM_TUNE_H */
