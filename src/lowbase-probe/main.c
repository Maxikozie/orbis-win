/* Load a high-address PRX, then hand off without returning to the eboot.
 * The PRX owns the destructive part of the experiment: it releases any eboot
 * segments overlapping 0x400000-0x55c000 and tests whether that PE range can
 * be reserved and mapped without replacing somebody else's memory.
 */
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { MODULE_INFO_BYTES = 0x160, MAX_SEGMENTS = 4, MAX_EXPORTS = 8 };
#define PRX_MAGIC UINT64_C(0x3143534544585250)

typedef struct { const char *name; const void *address; } ProbeExport;
typedef struct { uint64_t magic; uint32_t version, count; ProbeExport exports[]; } ProbeDescriptor;
typedef struct { const void *address; uint32_t size, protection; } ProbeSegment;
typedef void (*Handoff)(void);

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

static int module_segments(int32_t handle, ProbeSegment out[MAX_SEGMENTS], uint32_t *count,
                           char name[256])
{
    uint8_t raw[MODULE_INFO_BYTES] __attribute__((aligned(16)));
    memset(raw, 0, sizeof(raw));
    *(uint64_t *)raw = sizeof(raw);
    int rc = sceKernelGetModuleInfo((OrbisKernelModule)handle, (OrbisKernelModuleInfo *)raw);
    if (rc < 0) return rc;
    uint32_t n;
    memcpy(&n, raw + 0x148, sizeof(n));
    if (!n || n > MAX_SEGMENTS) return -1;
    if (name) { memcpy(name, raw + 8, 255); name[255] = 0; }
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

static const ProbeDescriptor *find_descriptor(const ProbeSegment *segments, uint32_t count)
{
    for (uint32_t s = 0; s < count; ++s) {
        const uint8_t *base = segments[s].address;
        if (!(segments[s].protection & 1u)) continue;
        for (size_t off = 0; off + 16 <= segments[s].size; off += 16) {
            uint64_t magic;
            memcpy(&magic, base + off, sizeof(magic));
            if (magic != PRX_MAGIC) continue;
            const ProbeDescriptor *d = (const ProbeDescriptor *)(base + off);
            if (d->version == 1 && d->count && d->count <= MAX_EXPORTS &&
                off + 16 + d->count * sizeof(ProbeExport) <= segments[s].size)
                return d;
        }
    }
    return NULL;
}

int main(void)
{
    ProbeSegment eboot[MAX_SEGMENTS];
    uint32_t eboot_count = 0;
    char name[256] = {0};
    say("lowbaseprobe: BEGIN target=0x400000..0x55c000");
    if (module_segments(0, eboot, &eboot_count, name)) {
        say("lowbaseprobe: eboot module-info failed");
        return 1;
    }
    say("lowbaseprobe: eboot '%s' has %u segments", name, eboot_count);
    for (uint32_t i = 0; i < eboot_count; ++i)
        say("lowbaseprobe: eboot seg%u %p size=0x%x prot=0x%x", i,
            eboot[i].address, eboot[i].size, eboot[i].protection);

    int result = 0;
    uint32_t handle = sceKernelLoadStartModule("/app0/sce_module/lowbase_handoff.prx",
                                                0, NULL, 0, NULL, &result);
    if (handle & 0x80000000u) {
        say("lowbaseprobe: PRX load failed handle=0x%x result=0x%x", handle, result);
        return 1;
    }
    say("lowbaseprobe: PRX loaded handle=0x%x result=0x%x", handle, result);

    ProbeSegment prx[MAX_SEGMENTS];
    uint32_t prx_count = 0;
    if (module_segments((int32_t)handle, prx, &prx_count, name)) {
        say("lowbaseprobe: PRX module-info failed");
        return 1;
    }
    const ProbeDescriptor *d = find_descriptor(prx, prx_count);
    if (!d) {
        say("lowbaseprobe: PRX export descriptor not found");
        return 1;
    }
    Handoff handoff = NULL;
    for (uint32_t i = 0; i < d->count; ++i)
        if (!strcmp(d->exports[i].name, "lowbase_probe_handoff"))
            handoff = (Handoff)d->exports[i].address;
    if (!handoff) {
        say("lowbaseprobe: handoff export missing");
        return 1;
    }
    say("lowbaseprobe: entering PRX handoff; eboot will not be resumed");
    handoff();
    say("lowbaseprobe: ERROR handoff returned");
    for (;;) sceKernelUsleep(1000000);
}
