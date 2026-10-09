/* Test PS4 low CPU aliases of graphics-accessible direct memory.
 * Reserve without overwrite before using MAP_FIXED: never replace a mapping
 * that belongs to the executable or its libraries. No GPU commands are sent.
 * This is the kernel feasibility test; GPU-copy validation comes afterwards.
 */
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

/* The SDK declares the output parameter as off_t instead of off_t*. */
extern int allocate_direct(size_t, size_t, int, int64_t *)
    __asm__("sceKernelAllocateMainDirectMemory");
enum { PAGE = 0x4000, BYTES = 4 * PAGE, FIXED = 0x10, NO_OVERWRITE = 0x80,
       GRAPHICS_RW = 0x33 };

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

static int check(volatile uint32_t *primary, volatile uint32_t *alias,
                 size_t bytes)
{
    unsigned bad = 0;
    for (size_t i = 0; i < bytes / sizeof(uint32_t); i++)
        primary[i] = 0x13579bdfu ^ (uint32_t)i;
    __sync_synchronize();
    for (size_t i = 0; i < bytes / sizeof(uint32_t); i++)
        bad += alias[i] != (0x13579bdfu ^ (uint32_t)i);
    for (size_t i = 0; i < bytes / sizeof(uint32_t); i++)
        alias[i] = 0x2468ace0u ^ (uint32_t)i;
    __sync_synchronize();
    for (size_t i = 0; i < bytes / sizeof(uint32_t); i++)
        bad += primary[i] != (0x2468ace0u ^ (uint32_t)i);
    note("mapprobe: bidirectional bytes=%zu mismatches=%u", bytes, bad);
    return bad != 0;
}

static void trial(int type)
{
    static const uintptr_t candidates[] = {0x10000000, 0x20000000,
                                           0x40000000, 0x60000000};
    int64_t physical = -1;
    void *primary = NULL;
    int rc = allocate_direct(BYTES, PAGE, type, &physical);
    note("mapprobe: allocate type=%#x bytes=%u rc=%#x physical=%#llx",
         type, BYTES, rc, (unsigned long long)physical);
    if (rc) return;
    rc = sceKernelMapDirectMemory(&primary, BYTES, GRAPHICS_RW, 0, physical, PAGE);
    note("mapprobe: primary type=%#x rc=%#x address=%p", type, rc, primary);
    if (rc) goto release;
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        void *low = (void *)candidates[i];
        if (low == primary) continue;
        rc = sceKernelReserveVirtualRange(&low, BYTES, FIXED | NO_OVERWRITE, PAGE);
        note("mapprobe: reserve wanted=%p got=%p rc=%#x", (void *)candidates[i], low, rc);
        if (rc) continue;
        if ((uintptr_t)low != candidates[i])
        {
            note("mapprobe: unexpected reservation; skipping fixed mapping");
            note("mapprobe: reservation cleanup rc=%#x", sceKernelMunmap(low, BYTES));
            continue;
        }
        void *alias = low;
        rc = sceKernelMapDirectMemory(&alias, BYTES, GRAPHICS_RW, FIXED, physical, PAGE);
        note("mapprobe: alias wanted=%p got=%p rc=%#x below4g=%u",
             low, alias, rc, (unsigned)((uintptr_t)alias + BYTES <= UINT64_C(0x100000000)));
        if (!rc)
        {
            if (alias != low) note("mapprobe: FAIL fixed map returned another address");
            else check(primary, alias, BYTES);
            rc = sceKernelMunmap(alias, BYTES);
            note("mapprobe: alias unmap rc=%#x", rc);
            if (rc) goto retain; /* keep physical backing while an alias may remain */
            ((volatile uint32_t *)primary)[0] = 0xabcdef01;
            note("mapprobe: primary survives alias unmap value=%#x",
                 ((volatile uint32_t *)primary)[0]);
        }
        else note("mapprobe: failed-map reservation cleanup rc=%#x", sceKernelMunmap(low, BYTES));
        /* Test an offset into the physical allocation, needed for suballocations. */
        alias = low;
        rc = sceKernelReserveVirtualRange(&alias, PAGE, FIXED | NO_OVERWRITE, PAGE);
        if (!rc && alias == low)
        {
            rc = sceKernelMapDirectMemory(&alias, PAGE, GRAPHICS_RW, FIXED, physical + PAGE, PAGE);
            note("mapprobe: offset alias physical+%u address=%p rc=%#x", PAGE, alias, rc);
            if (!rc && alias == low)
                check((volatile uint32_t *)((char *)primary + PAGE), alias, PAGE);
            rc = sceKernelMunmap(alias, PAGE);
            note("mapprobe: offset cleanup rc=%#x", rc);
            if (rc) goto retain;
        }
        else
        {
            note("mapprobe: offset reservation rc=%#x address=%p", rc, alias);
            if (!rc) sceKernelMunmap(alias, PAGE);
        }
    }
    rc = sceKernelMunmap(primary, BYTES);
    note("mapprobe: primary unmap rc=%#x", rc);
    if (rc) goto retain;
release:
    note("mapprobe: release rc=%#x", sceKernelReleaseDirectMemory(physical, BYTES));
    return;
retain:
    note("mapprobe: cleanup failed; retaining backing until title exit");
}

int main(void)
{
    note("mapprobe: BEGIN low graphics direct-memory aliases v1");
    trial(0); /* cached onion */
    trial(3); /* write-combined garlic */
    note("mapprobe: DONE; inspect mapping errors and bidirectional mismatches");
    for (;;) sleep(1); /* keep title alive for inspection and normal shell close */
}
