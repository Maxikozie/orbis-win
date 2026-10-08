/* Can a descriptor be duplicated on this console, and does it depend on what
 * kind of descriptor it is?
 *
 * wineserver's dup_fd_object (server/fd.c:1885) cannot duplicate the fd behind
 * an anonymous mapping. Every mechanism was refused:
 *
 *     dup()                 EPERM
 *     fcntl(F_DUPFD)        EINVAL
 *     dup2() onto a free fd failed for 64..255
 *     sceKernelRdup()       EPERM
 *
 * orbis-compat records dup2 onto fd 1 or 2 as EPERM -- "the descriptor table is
 * not ours to rearrange" -- but that was measured on the standard streams only.
 *
 * This asks the question properly: four mechanisms against five kinds of
 * descriptor. If only the shm fd is refused, the fix is local to anonymous
 * mappings. If every kind is refused, wineserver has to share descriptors
 * rather than duplicate them, which is a design change.
 *
 * Output goes to klog.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <orbis/libkernel.h>

/* ⚠ The SDK declares "void sceKernelRdup();" -- no return type, no arguments.
 * Bind the real symbol under a local name so the true signature can be used
 * without colliding with that stub. */
extern int pw_rdup(int fd) __asm__("sceKernelRdup");

static void say(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
}

/* An SCE call behind a POSIX name returns 0x8002_0000 | errno, not -1. */
static const char *sce(int r, char *out, size_t size)
{
    if ((unsigned)r >= 0x80000000u)
        snprintf(out, size, "SCE 0x%08x (errno %d)", (unsigned)r, r & 0xFFFF);
    else if (r < 0) snprintf(out, size, "-1 errno %d", errno);
    else snprintf(out, size, "fd %d", r);
    return out;
}

static int free_fd(void)
{
    for (int n = 64; n < 256; n++)
        if (fcntl(n, F_GETFD) == -1) return n;
    return -1;
}

static void probe(const char *what, int fd)
{
    char b[64];
    int r;

    if (fd < 0) { say("  %-18s could not create (errno %d)", what, errno); return; }

    errno = 0; r = dup(fd);
    say("  %-18s dup()            %s", what, sce(r, b, sizeof(b)));
    if (r >= 0 && (unsigned)r < 0x80000000u) close(r);

    errno = 0; r = fcntl(fd, F_DUPFD, 0);
    say("  %-18s fcntl(F_DUPFD)   %s", "", sce(r, b, sizeof(b)));
    if (r >= 0 && (unsigned)r < 0x80000000u) close(r);

    {
        int slot = free_fd();
        if (slot < 0) say("  %-18s dup2()           no free slot found", "");
        else { errno = 0; r = dup2(fd, slot);
               say("  %-18s dup2(->%d)       %s", "", slot, sce(r, b, sizeof(b)));
               if (r >= 0 && (unsigned)r < 0x80000000u) close(r); }
    }

    errno = 0; r = pw_rdup(fd);
    say("  %-18s sceKernelRdup()  %s", "", sce(r, b, sizeof(b)));
    if (r >= 0 && (unsigned)r < 0x80000000u) close(r);
    say("");
}

#define BASE "/data/prospero-win/prefixes/oblivion"

int main(void)
{
    int fd, pair[2];

    say("=== dup probe ===");
    say("  fcntl(F_GETFD) on fd 0 returns %d (if -1, the free-slot scan is meaningless)",
        fcntl(0, F_GETFD));
    say("");

    fd = open(BASE "/system.reg", O_RDONLY);
    probe("regular file", fd);
    if (fd >= 0) close(fd);

    fd = open(BASE, O_RDONLY);
    probe("directory", fd);
    if (fd >= 0) close(fd);

    fd = shm_open(SHM_ANON, O_RDWR, 0600);
    if (fd >= 0) ftruncate(fd, 0x4000);
    probe("shm SHM_ANON", fd);
    if (fd >= 0) close(fd);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0) {
        probe("unix socket", pair[0]);
        close(pair[0]); close(pair[1]);
    } else say("  unix socket        socketpair failed errno %d\n", errno);

    if (pipe(pair) == 0) {
        probe("pipe", pair[0]);
        close(pair[0]); close(pair[1]);
    } else say("  pipe               pipe() failed errno %d", errno);

    say("=== done ===");
    return 0;
}
