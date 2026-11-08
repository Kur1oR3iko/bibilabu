/* SPDX-License-Identifier: GPL-3.0-or-later
 * Trace before the SDK CRT, then enter the unmodified SDK startup routine.
 */
    .section .ppu_data.entry,"aw"
    .balign 8
    .globl wili_early_entry
wili_early_entry:
    .long .wili_early_start
    .long .TOC.

    .section .text.wili_early_start,"ax"
    .balign 4
    .globl .wili_early_start
.wili_early_start:
    lis 2,.TOC.@ha
    addi 2,2,.TOC.@l
    mflr 0
    std 0,16(1)
    stdu 1,-160(1)
    std 3,112(1)
    std 4,120(1)
    bl .wili_early_trace
    nop
    ld 3,112(1)
    ld 4,120(1)
    addi 1,1,160
    ld 0,16(1)
    mtlr 0
    b ._start
    .section .note.GNU-stack,"",@progbits
