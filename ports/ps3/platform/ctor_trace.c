// SPDX-License-Identifier: GPL-3.0-or-later
#include <ppu-types.h>
extern void wili_early_checkpoint(const char *message);

// The builder replaces four SDK .ctors pointers with these wrappers and fills
// the original descriptor addresses after sprxlinker. The allocator metaLock
// initializer is scheduled before the allocating file-path initializer.
volatile u64 wili_original_ctors[4] = {1, 1, 1, 1};

#define TRACE_CTOR(index, name) \
    void wili_ctor_##index(void) { \
        wili_early_checkpoint("CTOR_BEGIN " name); \
        ((void (*)(void))wili_original_ctors[index])(); \
        wili_early_checkpoint("CTOR_END " name); \
    }

TRACE_CTOR(0, "sbrk_init")
TRACE_CTOR(1, "__syscalls_init")
TRACE_CTOR(2, "__glob_file_init")
TRACE_CTOR(3, "init_metalock")

extern void __real_abort(void) __attribute__((noreturn));
void __wrap_abort(void)
{
    wili_early_checkpoint("CRT_ABORT_REQUESTED");
    __real_abort();
}
