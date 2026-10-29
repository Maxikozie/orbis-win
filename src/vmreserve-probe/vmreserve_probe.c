/* Why does sceKernelReserveVirtualRange refuse every fixed reservation inside
 * Wine? Measured there (package 1043, FW 12.02): rc=0x8002000c (ENOMEM) for
 * 54 of 54 calls, down to a 64 KiB PROT_NONE range, while plain mmap succeeds
 * at the same addresses. That refusal leaves Wine's reserved_areas empty, so
 * no range is registered for direct memory and the title drains its ~440 MiB
 * flexible pool in about 3.5 minutes.
 *
 * Wine varies four things at once (base, size, flags, alignment) and cannot
 * isolate any of them. This sweeps them one at a time in a bare title, and
 * first reports what sceKernelVirtualQuery says is actually at each base --
 * the question the Wine-side EEXIST has been hiding all day, since that errno
 * is synthesized by our own fallback and never came from the kernel.
 */
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { FIXED = 0x10, NO_OVERWRITE = 0x80 };

static void note(const char *fmt, ...)
{
    char line[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    size_t n = 0;
    while (line[n]) n++;
    line[n++] = '\n'; line[n] = 0;
    sceKernelDebugOutText(0, line);
}

static void describe(uintptr_t base)
{
    OrbisKernelVirtualQueryInfo qi;
    int rc;
    memset(&qi, 0, sizeof(qi));
    rc = sceKernelVirtualQuery((const void *)base, 1, &qi, sizeof(qi));
    qi.name[sizeof(qi.name) - 1] = 0;
    note("vmprobe: query base=%#zx rc=%#x start=%p end=%p prot=%#x type=%#x "
         "flex=%u direct=%u stack=%u pooled=%u committed=%u name=%s",
         (size_t)base, rc, qi.unk01, qi.unk02, qi.unk04, qi.unk05,
         qi.isFlexibleMemory, qi.isDirectMemory, qi.isStack,
         qi.isPooledMemory, qi.isCommitted, qi.name);
}

/* One reservation, reported with every input, then released if it succeeded
 * so the sweep does not poison the addresses it tests later. */
static void try_reserve(uintptr_t base, size_t size, int flags, size_t align,
                        const char *label)
{
    void *at = (void *)base;
    int rc = sceKernelReserveVirtualRange(&at, size, flags, align);
    note("vmprobe: %s base=%#zx size=%#zx flags=%#x align=%#zx -> rc=%#x got=%p%s",
         label, (size_t)base, size, flags, align, rc, at,
         rc ? "" : " GRANTED");
    if (!rc) note("vmprobe:   release rc=%#x", sceKernelMunmap(at, size));
}

int main(void)
{
    static const uintptr_t bases[] = {
        0x10000, 0x400000, 0x530000, 0x1000000, 0x10000000, 0x40000000,
    };
    /* 0x1009000 is FalloutNV.exe's image size; 0x130000 is the sub-range Wine
     * was refused at 0x400000. Both appear verbatim in the Wine logs. */
    static const size_t sizes[] = { 0x4000, 0x10000, 0x130000, 0x1009000 };
    static const int flagsets[] = { 0, FIXED, NO_OVERWRITE, FIXED | NO_OVERWRITE };
    static const size_t aligns[] = { 0, 0x4000, 0x10000, 0x200000 };

    note("vmprobe: start -- what is mapped at each base");
    for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); b++)
        describe(bases[b]);

    /* Wine's exact call, varying only the base. */
    note("vmprobe: --- Wine's call (FIXED|NO_OVERWRITE, align=0x4000), base swept");
    for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); b++)
        try_reserve(bases[b], 0x10000, FIXED | NO_OVERWRITE, 0x4000, "wine-call");

    /* Hold base and size; vary flags. */
    note("vmprobe: --- flags swept at 0x400000, size 0x10000, align 0x4000");
    for (unsigned f = 0; f < sizeof(flagsets) / sizeof(flagsets[0]); f++)
        try_reserve(0x400000, 0x10000, flagsets[f], 0x4000, "flags");

    /* Hold base, size and flags; vary alignment, including 0 and 2 MiB. */
    note("vmprobe: --- alignment swept at 0x400000, size 0x10000, FIXED|NO_OVERWRITE");
    for (unsigned a = 0; a < sizeof(aligns) / sizeof(aligns[0]); a++)
        try_reserve(0x400000, 0x10000, FIXED | NO_OVERWRITE, aligns[a], "align");

    /* Hold base, flags and alignment; vary size, including Wine's two. */
    note("vmprobe: --- size swept at 0x400000, FIXED|NO_OVERWRITE, align 0x4000");
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        try_reserve(0x400000, sizes[s], FIXED | NO_OVERWRITE, 0x4000, "size");

    /* No fixed address at all: does the call ever succeed in this process? */
    note("vmprobe: --- no base (kernel chooses), size 0x10000");
    try_reserve(0, 0x10000, 0, 0x4000, "anywhere");
    try_reserve(0, 0x10000, 0, 0, "anywhere-align0");

    note("vmprobe: done");
    for (;;) sceKernelUsleep(1000 * 1000);
    return 0;
}
