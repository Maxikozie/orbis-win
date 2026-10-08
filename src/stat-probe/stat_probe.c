/* Is st_dev stable, and do stat() and fstat() agree?
 *
 * wineserver's create_server_dir creates its directory, chdir()s in, open(".")s
 * it and compares st_dev/st_ino against the directory it just made. On this
 * console the comparison fails, and the directory name it builds from those
 * fields -- server-<st_dev>-<st_ino> -- changes between runs:
 *
 *     run 1   server-290bff49-7e78bd
 *     run 2   server-290bff65-7e78bd
 *
 * st_ino is stable, st_dev is not. This measures exactly that, on the same
 * file system the prefix lives on, and prints the raw bytes as well as the
 * fields: orbis-compat documents that the SDK's struct stat does not match the
 * layout the kernel fills (the shear measured on st_size and st_mode), so the
 * raw dump is what distinguishes a mistranslation from a volatile field.
 *
 * Output goes to klog.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <orbis/libkernel.h>

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
}

static void show(const char *what, const struct stat *st)
{
    say("  %-14s dev=0x%llx ino=0x%llx mode=0%o nlink=%u uid=%u size=%lld",
        what,
        (unsigned long long)st->st_dev, (unsigned long long)st->st_ino,
        (unsigned)st->st_mode, (unsigned)st->st_nlink,
        (unsigned)st->st_uid, (long long)st->st_size);
}

/* The raw bytes, because a field can be right and the struct still wrong. */
static void raw(const char *what, const void *p)
{
    const unsigned char *b = p;
    char line[200];
    int n = snprintf(line, sizeof(line), "  %-14s", what);
    for (int i = 0; i < 32 && n < (int)sizeof(line) - 4; i++)
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%02x%s", b[i], (i % 8) == 7 ? " " : "");
    say("%s", line);
}

#define DIR_BASE "/data/prospero-win/prefixes/oblivion"

int main(void)
{
    struct stat a, b, c;
    char dir[256];
    int fd;

    say("=== stat probe ===");
    say("sizeof(struct stat) = %zu  (caller side, this toolchain)", sizeof(struct stat));

    snprintf(dir, sizeof(dir), "%s/.statprobe", DIR_BASE);
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        say("mkdir %s failed errno=%d -- falling back to the prefix itself", dir, errno);
        snprintf(dir, sizeof(dir), "%s", DIR_BASE);
    }
    say("directory: %s", dir);

    memset(&a, 0, sizeof(a));
    if (stat(dir, &a) != 0) { say("stat failed errno=%d", errno); return 1; }
    show("stat #1", &a);  raw("stat #1 raw", &a);

    memset(&b, 0, sizeof(b));
    if (stat(dir, &b) != 0) { say("stat #2 failed errno=%d", errno); return 1; }
    show("stat #2", &b);
    say("  -> two stats of the SAME path agree on dev: %s, ino: %s",
        a.st_dev == b.st_dev ? "yes" : "NO", a.st_ino == b.st_ino ? "yes" : "NO");

    if ((fd = open(dir, O_RDONLY)) < 0) { say("open failed errno=%d", errno); return 1; }
    memset(&c, 0, sizeof(c));
    if (fstat(fd, &c) != 0) { say("fstat failed errno=%d", errno); close(fd); return 1; }
    show("fstat", &c);  raw("fstat raw", &c);
    close(fd);

    say("  -> stat vs fstat agree on dev: %s, ino: %s",
        a.st_dev == c.st_dev ? "yes" : "NO", a.st_ino == c.st_ino ? "yes" : "NO");
    say("     this is exactly the comparison wineserver makes and loses");

    say("=== done ===");
    return 0;
}
