// SPDX-License-Identifier: GPL-3.0-or-later
// Compile freestanding: these markers must not require constructors or libc.
#include <sys/file.h>
#include "probe_identity.h"

void wili_early_checkpoint(const char *message)
{
    char usb[] = "/dev_usb000/WILI_EARLY.LOG";
    const char *paths[3];
    u64 length = 0;
    int i, port;
    while (message[length]) length++;
    paths[0] = usb;
    paths[1] = "/dev_hdd0/tmp/WILI_EARLY.LOG";
    paths[2] = "/dev_hdd0/game/WILIPRB01/USRDIR/WILI_EARLY.LOG";
    for (i = 0; i < 3; i++) {
        int fd = -1;
        for (port = 0; port < (i == 0 ? 8 : 1); port++) {
            usb[10] = (char)('0' + port);
            if (!sysLv2FsOpen(paths[i], SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND,
                              &fd, 0666, 0, 0)) break;
        }
        if (fd < 0) continue;
        u64 done = 0;
        while (done < length) {
            u64 amount = 0;
            if (sysLv2FsWrite(fd, message + done, length - done, &amount) ||
                !amount || amount > length - done) break;
            done += amount;
        }
        u64 amount = 0;
        sysLv2FsWrite(fd, "\n", 1, &amount);
        sysLv2FsFsync(fd);
        sysLv2FsClose(fd);
    }
}

void wili_early_trace(void)
{
    wili_early_checkpoint(WILI_PROBE_ID " BEFORE_SDK_CRT");
}

static void __attribute__((constructor(101))) trace_constructor(void)
{
    wili_early_checkpoint(WILI_PROBE_ID " CRT_CONSTRUCTOR_REACHED");
}

