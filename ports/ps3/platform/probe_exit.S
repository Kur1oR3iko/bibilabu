/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../bootstrap/syscall_ids.h"
    .section .opd,"aw"
    .balign 8
    .globl wili_platform_exit
wili_platform_exit:
    .quad .wili_platform_exit,.TOC.@tocbase,0
    .section .text.wili_platform_exit,"ax"
    .balign 4
    .globl .wili_platform_exit
.wili_platform_exit:
    li 3,0
    li 11,WILI_SYS_PROCESS_EXIT
    sc
    li 3,0
    li 4,0
    li 5,0
    li 11,WILI_SYS_PROCESS_EXIT_LOWLEVEL
    sc
    li 3,1
    li 11,WILI_SYS_THREAD_EXIT
    sc
1:  lis 3,15
    ori 3,3,16960
    li 11,WILI_SYS_USLEEP
    sc
    b 1b
    .section .note.GNU-stack,"",@progbits
