/* SPDX-License-Identifier: MIT */
/*
 * tc275_lite/board_wdt_early.S — disable CPU0 + Safety WDT before CSA init.
 *
 * Button / standalone reset does not run OpenOCD's reset-end WDT poke.
 * Default WDT timeout is short enough that CSA construction can expire it
 * before ulmk_board_init(), cascading into class-4 then class-3 (FCX=0).
 *
 * Must not CALL (no CSA yet).  Caller leaves resume PC in A11; we ji %a11.
 */

	.section .text.board_wdt_early, "ax"
	.global ulmk_board_wdt_disable_early
	.type   ulmk_board_wdt_disable_early, @function

	.set WDTCPU0_CON0, 0xF0036100
	.set WDTCPU0_CON1, 0xF0036104
	.set WDTS_CON0,    0xF00360F0
	.set WDTS_CON1,    0xF00360F4

ulmk_board_wdt_disable_early:
	/* -------- CPU0 WDT -------- */
	movh.a  %a2, hi:WDTCPU0_CON0
	lea     %a2, [%a2] lo:WDTCPU0_CON0
	ld.w    %d0, [%a2]0
	extr.u  %d8, %d0, 2, 14		/* PW */
	mov     %d1, 0x3F
	xor     %d8, %d1			/* invert low 6 */
	extr.u  %d9, %d0, 16, 16		/* REL */

	extr.u  %d2, %d0, 1, 1		/* LCK? */
	jz      %d2, .L_cpu0_cle2
	sh      %d0, %d8, 2
	mov     %d1, 1
	or      %d0, %d1			/* ENDINIT=1 LCK=0 */
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_cpu0_cle2:
	sh      %d0, %d8, 2
	mov     %d1, 2
	or      %d0, %d1			/* ENDINIT=0 LCK=1 */
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_cpu0_wait0:
	ld.w    %d0, [%a2]0
	and     %d1, %d0, 1
	jnz     %d1, .L_cpu0_wait0

	movh.a  %a3, hi:WDTCPU0_CON1
	lea     %a3, [%a3] lo:WDTCPU0_CON1
	mov     %d0, 8				/* DR=1 */
	st.w    [%a3]0, %d0

	extr.u  %d2, %d0, 1, 1			/* reuse LCK path on CON0 */
	ld.w    %d0, [%a2]0
	extr.u  %d9, %d0, 16, 16
	extr.u  %d2, %d0, 1, 1
	jz      %d2, .L_cpu0_set2
	sh      %d0, %d8, 2
	mov     %d1, 1
	or      %d0, %d1
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_cpu0_set2:
	sh      %d0, %d8, 2
	mov     %d1, 3				/* ENDINIT=1 LCK=1 */
	or      %d0, %d1
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_cpu0_wait1:
	ld.w    %d0, [%a2]0
	and     %d1, %d0, 1
	jz      %d1, .L_cpu0_wait1

	/* -------- Safety WDT -------- */
	movh.a  %a2, hi:WDTS_CON0
	lea     %a2, [%a2] lo:WDTS_CON0
	ld.w    %d0, [%a2]0
	extr.u  %d8, %d0, 2, 14
	mov     %d1, 0x3F
	xor     %d8, %d1
	extr.u  %d9, %d0, 16, 16
	extr.u  %d2, %d0, 1, 1
	jz      %d2, .L_sfty_cle2
	sh      %d0, %d8, 2
	mov     %d1, 1
	or      %d0, %d1
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_sfty_cle2:
	sh      %d0, %d8, 2
	mov     %d1, 2
	or      %d0, %d1
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_sfty_wait0:
	ld.w    %d0, [%a2]0
	and     %d1, %d0, 1
	jnz     %d1, .L_sfty_wait0

	movh.a  %a3, hi:WDTS_CON1
	lea     %a3, [%a3] lo:WDTS_CON1
	mov     %d0, 8
	st.w    [%a3]0, %d0

	ld.w    %d0, [%a2]0
	extr.u  %d9, %d0, 16, 16
	extr.u  %d2, %d0, 1, 1
	jz      %d2, .L_sfty_set2
	sh      %d0, %d8, 2
	mov     %d1, 1
	or      %d0, %d1
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_sfty_set2:
	sh      %d0, %d8, 2
	mov     %d1, 3
	or      %d0, %d1
	sh      %d1, %d9, 16
	or      %d0, %d1
	st.w    [%a2]0, %d0
.L_sfty_wait1:
	ld.w    %d0, [%a2]0
	and     %d1, %d0, 1
	jz      %d1, .L_sfty_wait1

	ji      %a11
	.size   ulmk_board_wdt_disable_early, . - ulmk_board_wdt_disable_early
