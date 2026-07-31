/* SPDX-License-Identifier: MIT */
#ifndef PSRAM_STRESS_H
#define PSRAM_STRESS_H

/*
 * March over free PSRAM while DW_GDMA is scanning the dual FBs.
 * Returns 0 on pass, -1 on the first mismatch (already logged).
 */
int psram_stress_under_scanout(void);

#endif /* PSRAM_STRESS_H */
