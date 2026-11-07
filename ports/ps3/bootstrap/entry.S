/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compact LV2 executable entry descriptor. No C runtime initialization.
 */
#include "syscall_ids.h"
    .section .data.entry,"aw"
    .balign 8
    .globl _start
_start:
    .long .probe_entry
    .long .TOC.

    .section .text.probe_entry,"ax"
    .balign 4
    .globl .probe_entry
.probe_entry:
    lis 2,.TOC.@ha
    addi 2,2,.TOC.@l
    stdu 1,-128(1)
    bl .boot_main
    nop
    li 3,0
    li 11,WILI_SYS_PROCESS_EXIT
    sc
    /* Both process exits are documented non-returning calls. If the first
     * unexpectedly returns, use the low-level variant with explicit args. */
    li 3,0
    li 4,0
    li 5,0
    li 11,WILI_SYS_PROCESS_EXIT_LOWLEVEL
    sc
    li 3,1
    li 11,WILI_SYS_THREAD_EXIT
    sc
    /* Last-resort idle guard, never a CPU-burning tight branch. */
1:  lis 3,15
    ori 3,3,16960
    li 11,WILI_SYS_USLEEP
    sc
    b 1b
    .section .note.GNU-stack,"",@progbits
