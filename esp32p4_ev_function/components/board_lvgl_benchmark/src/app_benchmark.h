/* SPDX-License-Identifier: MIT */
#ifndef APP_BENCHMARK_H
#define APP_BENCHMARK_H

void app_benchmark_run(void);

/*
 * LV_ASSERT_HANDLER.  Reports and returns: halting here hides where the run
 * died, and a failed assertion that goes on to fault is at least visible as
 * a kernel trap.
 */
void app_lvgl_assert(void);

#endif /* APP_BENCHMARK_H */
