/* SPDX-License-Identifier: GPL-3.0-or-later
 * LV2 process calls, cross-checked against RPCS3's lv2 syscall table:
 * https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/lv2/lv2.cpp
 * 1 is process_getpid, NOT process exit.
 */
#define WILI_SYS_PROCESS_EXIT 3
#define WILI_SYS_PROCESS_EXIT_LOWLEVEL 22
#define WILI_SYS_THREAD_EXIT 41
#define WILI_SYS_USLEEP 141
