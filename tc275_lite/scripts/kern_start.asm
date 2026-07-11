
/elf/ulmk:     file format elf32-tricore


Disassembly of section .kernel_text:

a0002374 <ulmk_kern_start>:
a0002374:	40 ae       	mov.aa %a14,%sp
a0002376:	20 30       	sub.a %sp,48
a0002378:	d9 e2 d4 ff 	lea %a2,[%a14]-44
a000237c:	3b c0 02 20 	mov %d2,44
a0002380:	02 25       	mov %d5,%d2
a0002382:	82 04       	mov %d4,0
a0002384:	40 24       	mov.aa %a4,%a2
a0002386:	6d 00 d1 38 	call a0009528 <memset>
a000238a:	82 14       	mov %d4,1
a000238c:	6d 00 53 33 	call a0008a32 <ulmk_board_hil_mark>
a0002390:	6d 00 1b 33 	call a00089c6 <ulmk_board_init>
a0002394:	82 24       	mov %d4,2
a0002396:	6d 00 4e 33 	call a0008a32 <ulmk_board_hil_mark>
a000239a:	91 10 00 2a 	movh.a %a2,40961
a000239e:	d9 26 40 b9 	lea %a6,[%a2]-26944 <a00096c0 <__ulmk_kernel_data_load>>
a00023a2:	91 00 00 27 	movh.a %a2,28672
a00023a6:	d9 25 08 00 	lea %a5,[%a2]8 <70000008 <idle_thread_g>>
a00023aa:	91 00 00 27 	movh.a %a2,28672
a00023ae:	d9 24 00 00 	lea %a4,[%a2]0 <70000000 <g_next_src_slot>>
a00023b2:	6d ff a7 ff 	call a0002300 <copy_words>
a00023b6:	91 00 00 27 	movh.a %a2,28672
a00023ba:	d9 25 08 b1 	lea %a5,[%a2]4808 <700012c8 <_ulmk_kernel_bss_end>>
a00023be:	91 00       	.hword 0x0091
