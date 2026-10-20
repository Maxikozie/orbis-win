/* Small title entry: load the high-address Wine PRX and call its descriptor
 * export. It must never return to the eboot after the PRX unmaps it. */
#include <orbis/libkernel.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { MODULE_INFO_BYTES = 0x160, MAX_SEGMENTS = 4, MAX_EXPORTS = 8 };
#define PRX_MAGIC UINT64_C(0x3143534544585250)
typedef struct { const char *name; const void *address; } Export;
typedef struct { uint64_t magic; uint32_t version, count; Export exports[]; } Descriptor;
typedef struct { const void *address; uint32_t size, protection; } Segment;
typedef void (*Entry)(int, char **);

static const char *const libkernel_symbols[] = {
#include "libkernel-symbols.inc"
};

static void report_mapping(const char *stage, void *address)
{
    OrbisKernelVirtualQueryInfo info;
    char line[192];
    int rc;

    memset(&info, 0, sizeof(info));
    rc = sceKernelVirtualQuery(address, 1, &info, sizeof(info));
    snprintf(line, sizeof(line),
             "relocwine: exec-probe stage=%s addr=%p query=%d range=%p-%p prot=%#x type=%#x flex=%u direct=%u committed=%u\n",
             stage, address, rc, info.unk01, info.unk02, info.unk04,
             info.unk05, info.isFlexibleMemory, info.isDirectMemory,
             info.isCommitted);
    sceKernelDebugOutText(0, line);
}

static void exec_protection_probe(void)
{
    enum { TEST_PAGE = 0x4000 };
    char line[160];
    void *rw, *rwx;
    int rc;

    sceKernelDebugOutText(0, "relocwine: exec-probe stage=begin\n");
    rw = mmap(NULL, TEST_PAGE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    snprintf(line, sizeof(line), "relocwine: exec-probe stage=map-rw addr=%p\n", rw);
    sceKernelDebugOutText(0, line);
    if (rw != MAP_FAILED) {
        report_mapping("rw-before", rw);
        rc = sceKernelMprotect(rw, TEST_PAGE,
                               PROT_READ | PROT_WRITE | PROT_EXEC);
        snprintf(line, sizeof(line), "relocwine: exec-probe stage=sce-mprotect rc=%d\n", rc);
        sceKernelDebugOutText(0, line);
        report_mapping("after-sce-mprotect", rw);
        rc = mprotect(rw, TEST_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC);
        snprintf(line, sizeof(line), "relocwine: exec-probe stage=libc-mprotect rc=%d\n", rc);
        sceKernelDebugOutText(0, line);
        report_mapping("after-libc-mprotect", rw);
        mprotect(rw, TEST_PAGE, PROT_READ | PROT_WRITE);
        munmap(rw, TEST_PAGE);
    }

    rwx = mmap(NULL, TEST_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    snprintf(line, sizeof(line), "relocwine: exec-probe stage=map-rwx addr=%p\n", rwx);
    sceKernelDebugOutText(0, line);
    if (rwx != MAP_FAILED) {
        report_mapping("direct-rwx", rwx);
        mprotect(rwx, TEST_PAGE, PROT_READ | PROT_WRITE);
        munmap(rwx, TEST_PAGE);
    }
    sceKernelDebugOutText(0, "relocwine: exec-probe stage=end\n");
}

/* Resolve the repeated SIGSYS PCs against this console's actual libkernel
 * exports. The core's module map puts them at text offsets 0x28bc and 0x75c2;
 * the matching function names will distinguish a refused syscall from a
 * coincidentally similar offset in the eboot. */
static void diagnose_libkernel(void)
{
    OrbisKernelModule modules[256];
    size_t available = 0;
    int rc = sceKernelGetModuleList(modules, sizeof(modules) / sizeof(modules[0]), &available);
    char line[160];

    if (rc != 0) {
        snprintf(line, sizeof(line), "relocwine: libkernel module list failed 0x%x\n", rc);
        sceKernelDebugOutText(0, line);
        return;
    }

    size_t count = available < sizeof(modules) / sizeof(modules[0]) ?
                   available : sizeof(modules) / sizeof(modules[0]);
    int found = 0;
    for (size_t i = 0; i < count; ++i) {
        OrbisKernelModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(modules[i], &info) != 0 || !strstr(info.name, "libkernel"))
            continue;
        found = 1;

        /* Check ordinary executable mappings in early eboot startup, before
         * Wine has entered virtual_init or a memory-protection callback. */
        exec_protection_probe();

        uintptr_t targets[] = { 0x28bc, 0x75c2 };
        uintptr_t target_addresses[2] = { 0, 0 }, nearest[2] = { 0, 0 };
        const char *nearest_name[2] = { NULL, NULL };
        for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
            for (uint32_t s = 0; s < info.segmentCount && s < 4; ++s) {
                OrbisKernelModuleSegmentInfo *seg = &info.segmentInfo[s];
                if ((seg->prot & ORBIS_KERNEL_PROT_CPU_EXEC) && targets[t] < seg->size) {
                    target_addresses[t] = (uintptr_t)seg->address + targets[t];
                    break;
                }
            }
            if (!target_addresses[t]) {
                snprintf(line, sizeof(line), "relocwine: libkernel target +0x%lx has no text segment\n",
                         (unsigned long)targets[t]);
                sceKernelDebugOutText(0, line);
            }
        }

        for (size_t n = 0; n < sizeof(libkernel_symbols) / sizeof(libkernel_symbols[0]); ++n) {
            void *address = NULL;
            if (sceKernelDlsym((int32_t)modules[i], libkernel_symbols[n], &address) != 0 || !address)
                continue;
            uintptr_t value = (uintptr_t)address;
            for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
                uintptr_t target = target_addresses[t];
                if (target && value <= target && target - value < 0x1000 && value > nearest[t]) {
                    nearest[t] = value;
                    nearest_name[t] = libkernel_symbols[n];
                }
            }
        }

        for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
            if (nearest_name[t])
                snprintf(line, sizeof(line), "relocwine: libkernel +0x%lx nearest export %s +0x%lx\n",
                         (unsigned long)targets[t], nearest_name[t],
                         (unsigned long)(target_addresses[t] - nearest[t]));
            else
                snprintf(line, sizeof(line), "relocwine: libkernel +0x%lx no nearby named export\n",
                         (unsigned long)targets[t]);
            sceKernelDebugOutText(0, line);
        }
    }
    if (!found) sceKernelDebugOutText(0, "relocwine: libkernel module not found in module list\n");
}

static int segments(int32_t handle, Segment out[MAX_SEGMENTS], uint32_t *count)
{
    uint8_t raw[MODULE_INFO_BYTES] __attribute__((aligned(16)));
    memset(raw, 0, sizeof(raw));
    *(uint64_t *)raw = sizeof(raw);
    int rc = sceKernelGetModuleInfo((OrbisKernelModule)handle, (OrbisKernelModuleInfo *)raw);
    if (rc < 0) return rc;
    uint32_t n;
    memcpy(&n, raw + 0x148, sizeof(n));
    if (!n || n > MAX_SEGMENTS) return -1;
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t *item = raw + 0x108 + i * 16;
        uint64_t address;
        memcpy(&address, item, 8);
        memcpy(&out[i].size, item + 8, 4);
        memcpy(&out[i].protection, item + 12, 4);
        out[i].address = (const void *)(uintptr_t)address;
        if (!address || !out[i].size) return -1;
    }
    *count = n;
    return 0;
}

static const Descriptor *find_descriptor(const Segment *items, uint32_t count)
{
    for (uint32_t s = 0; s < count; ++s) {
        const uint8_t *base = items[s].address;
        if (!(items[s].protection & 1u)) continue;
        for (size_t off = 0; off + 16 <= items[s].size; off += 16) {
            uint64_t magic;
            memcpy(&magic, base + off, sizeof(magic));
            if (magic != PRX_MAGIC) continue;
            const Descriptor *d = (const Descriptor *)(base + off);
            if (d->version == 1 && d->count && d->count <= MAX_EXPORTS &&
                off + 16 + d->count * sizeof(Export) <= items[s].size)
                return d;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int result = 0;
    diagnose_libkernel();
    uint32_t handle = sceKernelLoadStartModule(
        "/app0/sce_module/relocated_wine_app.prx", 0, NULL, 0, NULL, &result);
    if (handle & 0x80000000u) {
        sceKernelDebugOutText(0, "relocwine: app PRX load failed\n");
        for (;;) sceKernelUsleep(1000000);
    }
    Segment items[MAX_SEGMENTS];
    uint32_t count = 0;
    const Descriptor *d = NULL;
    if (!segments((int32_t)handle, items, &count)) d = find_descriptor(items, count);
    Entry entry = NULL;
    if (d) for (uint32_t i = 0; i < d->count; ++i)
        if (!strcmp(d->exports[i].name, "pw_relocated_wine_start"))
            entry = (Entry)d->exports[i].address;
    if (!entry) {
        sceKernelDebugOutText(0, "relocwine: PRX entry descriptor missing\n");
        for (;;) sceKernelUsleep(1000000);
    }
    sceKernelDebugOutText(0, "relocwine: entering high-address Wine PRX\n");
    entry(argc, argv);
    for (;;) sceKernelUsleep(1000000);
}
