// SPDX-License-Identifier: GPL-3.0-or-later
// This diagnostic has no GUI, libc, imported modules, or media calls.
#include <sys/process.h>
#include <sys/file.h>
#include <sys/systime.h>

SYS_PROCESS_PARAM(1001, 0x100000);

static int sinks[3] = {-1, -1, -1};

static void write_all(int fd, const char *p, u64 size)
{
    while (size) {
        u64 written = 0;
        int rc = sysLv2FsWrite(fd, p, size, &written);
        if (rc || !written || written > size) return;
        p += written; size -= written;
    }
}

static void line(const char *message)
{
    u64 size = 0;
    int i;
    while (message[size]) size++;
    for (i = 0; i < 3; ++i) if (sinks[i] >= 0) {
        write_all(sinks[i], message, size);
        write_all(sinks[i], "\n", 1);
        sysLv2FsFsync(sinks[i]);
    }
}

static void number(const char *name, u64 value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[96]; unsigned n = 0; int shift;
    while (*name && n < 60) buf[n++] = *name++;
    buf[n++] = '='; buf[n++] = '0'; buf[n++] = 'x';
    for (shift = 60; shift >= 0; shift -= 4) buf[n++] = digits[(value >> shift) & 15];
    buf[n] = 0;
    line(buf);
}

static int open_sink(const char *path)
{
    int fd = -1;
    sysFSStat st;
    if (sysLv2FsStat(path, &st) == 0 && st.st_size > 512 * 1024) return -1;
    if (sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND, &fd, 0666, 0, 0)) return -1;
    return fd;
}

void boot_main(void)
{
    int i;
    char usb[] = "/dev_usb000/WILI_BOOT.LOG";
    sinks[1] = open_sink("/dev_hdd0/tmp/WILI_BOOT.LOG");
    sinks[2] = open_sink("/dev_hdd0/game/WILIPRB01/USRDIR/WILI_BOOT.LOG");
    for (i = 0; i < 8; ++i) {
        usb[10] = (char)('0' + i);
        sinks[0] = open_sink(usb);
        if (sinks[0] >= 0) break;
    }
    line("WILIWILI_PS3_BOOT_02 ENTRY_REACHED");
    line("scope=boot_trace_only no_video no_network no_game_modifications");
    number("usb_log_open", sinks[0] >= 0);
    number("hdd_tmp_log_open", sinks[1] >= 0);
    number("app_log_open", sinks[2] >= 0);
    number("timebase_frequency", sysGetTimebaseFrequency());
    line("BEFORE_SLEEP expected_black_screen_seconds=2");
    number("sleep_rc", (u32)sysUsleep(2000000));
    line("CLOSE_HDD_LOGS_BEGIN");
    for (i = 1; i < 3; ++i) if (sinks[i] >= 0) {
        int rc = sysLv2FsClose(sinks[i]);
        sinks[i] = -1;
        number(i == 1 ? "hdd_tmp_close_rc" : "app_close_rc", (u32)rc);
    }
    line("EXIT_REQUESTED process_syscall=3 fallback=22");
    if (sinks[0] >= 0) { sysLv2FsClose(sinks[0]); sinks[0] = -1; }
}
