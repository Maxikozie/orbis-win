/* Runs only after control has left the eboot. It never returns into code that
 * may have just been unmapped. All output uses the kernel debug sink so the
 * result remains available after releasing the eboot's data segment.
 */
#include <orbis/libkernel.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { MODULE_INFO_BYTES = 0x160, MAX_SEGMENTS = 4, PAGE = 0x4000,
       RANGE_BYTES = 0x15c000, RESERVE_FLAGS = 0x10 | 0x80 };
#define RANGE_BASE ((uintptr_t)0x400000)
#define RANGE_END  (RANGE_BASE + RANGE_BYTES)

typedef struct { const void *address; uint32_t size, protection; } ProbeSegment;

static void say(const char *fmt, ...)
{
    char line[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line) - 2, fmt, args);
    va_end(args);
    size_t n = strlen(line);
    line[n++] = '\n';
    line[n] = 0;
    sceKernelDebugOutText(0, line);
}

static int get_eboot_segments(ProbeSegment out[MAX_SEGMENTS], uint32_t *count)
{
    uint8_t raw[MODULE_INFO_BYTES] __attribute__((aligned(16)));
    memset(raw, 0, sizeof(raw));
    *(uint64_t *)raw = sizeof(raw);
    int rc = sceKernelGetModuleInfo((OrbisKernelModule)0, (OrbisKernelModuleInfo *)raw);
    if (rc < 0) return rc;
    uint32_t n;
    memcpy(&n, raw + 0x148, sizeof(n));
    if (!n || n > MAX_SEGMENTS) return -1;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t *entry = raw + 0x108 + i * 16;
        uint64_t address;
        memcpy(&address, entry, 8);
        memcpy(&out[i].size, entry + 8, 4);
        memcpy(&out[i].protection, entry + 12, 4);
        out[i].address = (const void *)(uintptr_t)address;
        if (!address || !out[i].size) return -1;
    }
    *count = n;
    return 0;
}

__attribute__((visibility("default"), noreturn))
void lowbase_probe_handoff(void)
{
    ProbeSegment segments[MAX_SEGMENTS];
    uint32_t count = 0;
    say("lowbaseprobe: PRX active; checking low range before release");
    if (get_eboot_segments(segments, &count)) {
        say("lowbaseprobe: PRX could not read eboot segments; stopping safely");
        for (;;) sceKernelUsleep(1000000);
    }

    void *wanted = (void *)RANGE_BASE;
    int rc = sceKernelReserveVirtualRange(&wanted, RANGE_BYTES, RESERVE_FLAGS, PAGE);
    say("lowbaseprobe: pre-release reserve wanted=%p got=%p rc=0x%x (occupied is expected)",
        (void *)RANGE_BASE, wanted, rc);
    if (!rc) {
        int cleanup = sceKernelMunmap(wanted, RANGE_BYTES);
        say("lowbaseprobe: pre-release reservation cleanup rc=0x%x", cleanup);
    }

    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t base = (uintptr_t)segments[i].address;
        uintptr_t end = base + segments[i].size;
        if (base >= RANGE_END || end <= RANGE_BASE) continue;
        say("lowbaseprobe: releasing eboot seg%u %p..%p size=0x%x", i,
            (void *)base, (void *)end, segments[i].size);
        rc = sceKernelMunmap((void *)base, segments[i].size);
        say("lowbaseprobe: eboot seg%u munmap rc=0x%x", i, rc);
    }

    wanted = (void *)RANGE_BASE;
    rc = sceKernelReserveVirtualRange(&wanted, RANGE_BYTES, RESERVE_FLAGS, PAGE);
    say("lowbaseprobe: post-release reserve wanted=%p got=%p rc=0x%x",
        (void *)RANGE_BASE, wanted, rc);
    if (!rc && wanted == (void *)RANGE_BASE) {
        void *mapped = NULL;
        rc = sceKernelMmap(wanted, PAGE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0, &mapped);
        say("lowbaseprobe: map first page over reservation got=%p rc=0x%x", mapped, rc);
        if (!rc && mapped == (void *)RANGE_BASE) {
            volatile uint32_t *word = mapped;
            word[0] = 0x4c4f5742u;
            __sync_synchronize();
            say("lowbaseprobe: first-page readback=0x%08x expected=0x4c4f5742", word[0]);
            /* Keep the probe's reservation/mapping alive until the user closes it. */
        }
    }
    say("lowbaseprobe: HANDOFF COMPLETE; PRX remains active, do not expect return to eboot");
    for (;;) sceKernelUsleep(1000000);
}
