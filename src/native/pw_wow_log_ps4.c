/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* A diagnostic channel for the WoW64 CPU backend that actually arrives.
 *
 * ⚠ This module has no usable output otherwise. Its stderr goes to a sink
 * ntdll owns and a PRX carries its own libc, so fprintf lands nowhere -- which
 * silently hid both the diagnostics added here and prospero-win's own "no
 * memory for a thread's translator". sceKernelDebugOutText has never produced
 * a line from this module either, though the symbol is imported exactly as
 * wineserver's is, and that one does reach klog.
 *
 * So write to /data, which the title already has open for the prefix, and pull
 * the file off with scripts/ftp-get.py. The file outlives the process, holds
 * more than a fault storm would fit through klog, and does not depend on which
 * module the line came from. klog is still attempted, so that a line shows up
 * live when it can.
 *
 * Opened and closed per line: a translator that dies mid-block must not take
 * the last few lines with it, and this is for faults, not throughput. */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Beside the title's own session logs. prospero-win makes that directory, and
 * both are pulled off with scripts/ftp-get.py. */
#define PW_WOW_LOG_DIR  "/data/prospero-win/logs"
#define PW_WOW_LOG_PATH PW_WOW_LOG_DIR "/wowprospero.log"

int sceKernelDebugOutText(int channel, const char *text);

void pw_wow_log(const char *format, ...);

void pw_wow_log(const char *format, ...)
{
    char line[256];
    va_list args;
    int length;
    int fd;

    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length <= 0) return;
    if (length > (int)sizeof(line) - 1) length = (int)sizeof(line) - 1;

    sceKernelDebugOutText(0, line);

    /* ⚠ 0777/0666 and a chmod: GoldHEN's FTP server does not run as the title,
     * so anything it makes 0700 cannot be read off the console afterwards --
     * which is exactly how the title's own session logs ended up unreadable. */
    if ((fd = open(PW_WOW_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
        (void)mkdir("/data/prospero-win", 0777);
        (void)mkdir(PW_WOW_LOG_DIR, 0777);
        (void)chmod(PW_WOW_LOG_DIR, 0777);
        if ((fd = open(PW_WOW_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) return;
    }
    (void)fchmod(fd, 0666);   /* the title's umask masks the mode above */
    (void)!write(fd, line, (size_t)length);
    close(fd);
}
