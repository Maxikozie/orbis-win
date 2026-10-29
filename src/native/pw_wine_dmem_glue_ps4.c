/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* ⚠ A COPY OF prospero-win's wine/ps5/pw_wine_dmem_ps5.c WITH PS4 CONSTANTS.
 *
 * Patch 0600 routes ntdll's anonymous mmap onto direct memory and calls five
 * entry points — __wine_ps5_{dmem_region,mmap,munmap,mprotect,memory_stats} —
 * which live in that file and nowhere else. Without them create-fself refuses:
 *
 *     Failed to build FSELF: missing library for symbol (__wine_ps5_mmap)
 *
 * A .so may leave a symbol undefined; a PRX may not.
 *
 * Almost all of the file is portable logic over pw_wine_dmem's vtable, and it
 * declares its own kernel externs rather than using the SDK headers — which is
 * fortunate, because the OpenOrbis header for sceKernelAllocateMainDirectMemory
 * declares its out-parameter BY VALUE and would silently lose the offset.
 *
 * Only the memory-type constant actually differs, and it matters:
 *   PS5  MEMORY_TYPE 0x0c
 *   PS4  WB_ONION 0x0, WC_GARLIC 0x3, WB_GARLIC 0xA — 0x0c is not a type here
 * A wrong type is not rejected loudly; it is rejected at allocation time and
 * reads as "no direct memory available". */
/* The console side of pw_wine_dmem, linked into ntdll.prx: libkernel's
 * direct memory behind the mmap, munmap and mprotect calls of ntdll's
 * virtual memory manager (patch 0600 routes them here), for the address
 * ranges it reserves up front. Everything else passes straight through.
 *
 * The recipe is the one ps5-xash3d's heap runs on (FW 12.02): main direct
 * memory, mapped read-write at a fixed address over a reservation, then
 * protected with sceKernelMprotect; execute permission is refused at map
 * time but granted by mprotect. A self-check maps, writes, protects and
 * frees one page before the first region is taken; if any step fails, the
 * regions stay ordinary reservations and every call passes through. */
#include "pw_wine_dmem.h"
#include "pw_wine_heap.h"
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>

/* ⚠ Which thread is migrating. Needed to tell a concurrency race (migration on
 * one thread, guest fault on another) from a failed migration whose own thread
 * then touches the page -- the two need opposite fixes, and the traces carried
 * no thread id. Printed into the same klog channel as the fault traces so the
 * causal chain sits in ONE stream: klog and ps5log timestamps are skewed by
 * ~0.4 s, which invalidated several cross-stream timing comparisons. */
static unsigned long glue_tid(void)
{
    return (unsigned long)(uintptr_t)pthread_self();
}

extern int sceKernelReserveVirtualRange(void **address, size_t bytes, int flags, size_t alignment);
extern int sceKernelAllocateMainDirectMemory(size_t bytes, size_t alignment, int type, int64_t *offset);
extern int sceKernelMapDirectMemory(void **address, size_t bytes, int protection, int flags,
                                    int64_t offset, size_t alignment);
extern int sceKernelMprotect(const void *address, size_t bytes, int protection);
extern int sceKernelVirtualQuery(const void *address, int flags, void *info, size_t size);
extern int sceKernelDebugOutText(int channel, const char *text);
extern int sceKernelMunmap(void *address, size_t bytes);
extern int sceKernelReleaseDirectMemory(int64_t offset, size_t bytes);
extern int sceKernelAvailableDirectMemorySize(off_t start, off_t end, size_t alignment,
                                              off_t *phys_out, size_t *size_out);
extern size_t sceKernelGetDirectMemorySize(void);

static void force_exec_trace(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    sceKernelDebugOutText(0, line);
}

enum {
    PAGE = 0x4000,
    KERNEL_FIXED = 0x10,
    KERNEL_NO_OVERWRITE = 0x80,
    MEMORY_TYPE = 0x0,        /* ORBIS_KERNEL_WB_ONION: cached, CPU-coherent.
                             * Wine's anonymous memory is read far more than it is
                             * written, so WC_GARLIC (the scanout's type) would work
                             * and be slow in a way nothing reports. */
    CPU_READ_WRITE = 0x03,
    CPU_GPU_READ_WRITE = 0x33,  /* PS4: PROT_CPU_RW (0x3) | PROT_GPU_RW (0x30) */
    RUNS = 32768,
};

/* WINE_PS5_WAIT_WATCHDOG: a direct-memory call slower than 50 ms is
 * logged (prospero-win#250). */
static uint64_t slow_start(void)
{
    static int enabled = -1;
    struct timespec ts;

    if (enabled == -1) enabled = getenv("WINE_PS5_WAIT_WATCHDOG") != NULL;
    if (!enabled) return 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static void slow_end(uint64_t start, const char *what, size_t bytes)
{
    struct timespec ts;
    uint64_t now;

    if (!start) return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
    if (now - start > 50000)
        fprintf(stderr, "wine-ps5: slow dmem %s bytes=%zu ms=%llu\n", what, bytes,
                (unsigned long long)((now - start) / 1000u));
}

/* CPU read-write, unless the kernel wants the GPU bits too. */
static int map_protection = CPU_READ_WRITE;

/* Layout used by sceKernelVirtualQuery in the PS4 runtime. Keep the field
 * order and widths explicit here because this diagnostic is answering whether
 * the hardware mapping actually has execute permission. */
typedef struct {
    uintptr_t start, end;
    int64_t offset;
    int32_t protection, memory_type;
    uint8_t flags;
    char name[32];
} PwPs4VirtualQueryInfo;

_Static_assert(offsetof(PwPs4VirtualQueryInfo, offset) == 2 * sizeof(void *),
               "unexpected sceKernelVirtualQuery layout");

/* Native WoW64 reached a PE32 image page that Wine described as executable,
 * but the CPU reported a present instruction-fetch protection fault. Use
 * libkernel's protection entry point first for executable mappings, as the
 * direct-memory path already does. Keep mprotect as a compatibility fallback
 * and record the result for low guest addresses in a bounded trace. */
static int ps4_host_mprotect(void *address, size_t bytes, int protection)
{
    static unsigned logged;
    static unsigned fault_page_rwx_tested;
    static unsigned fault_page_host_protect_logged;
    int kernel_rc, libc_rc;

    if (!(protection & PROT_EXEC))
    {
        int rc = mprotect(address, bytes, protection);
        uintptr_t start = (uintptr_t)address;
        uintptr_t end = start + bytes;

        if (start <= 0x004a8330u && end > 0x004a8330u &&
            __atomic_fetch_add(&fault_page_host_protect_logged, 1, __ATOMIC_RELAXED) < 32)
        {
            PwPs4VirtualQueryInfo info;
            int query_rc;
            char line[256];

            memset(&info, 0, sizeof(info));
            query_rc = sceKernelVirtualQuery((const void *)0x004a8330u, 1,
                                             &info, sizeof(info));
            if (query_rc == 0) info.name[sizeof(info.name) - 1] = '\0';
            snprintf(line, sizeof(line),
                     "wine-ps4: fault-page host-mprotect addr=%p bytes=%zu prot=%#x rc=%d query=%d range=%p-%p effective=%#x name=%s\n",
                     address, bytes, protection, rc, query_rc,
                     (void *)info.start, (void *)info.end, info.protection,
                     query_rc == 0 ? info.name : "?");
            sceKernelDebugOutText(0, line);
        }
        return rc;
    }

    kernel_rc = sceKernelMprotect(address, bytes, protection);
    libc_rc = kernel_rc ? mprotect(address, bytes, protection) : 0;

    /* The 0948 raw frame reports an instruction-fetch protection fault at
     * 0x004a8330 even though this mprotect range covers the image. Ask the
     * kernel for that exact page's effective mapping after the call. */
    if ((uintptr_t)address <= 0x004a8330u &&
        (uintptr_t)address + bytes > 0x004a8330u)
    {
        PwPs4VirtualQueryInfo info;
        int query_rc, rwx_rc = -1, final_query_rc;
        int32_t before_prot;
        memset(&info, 0, sizeof(info));
        query_rc = sceKernelVirtualQuery((const void *)0x004a8330u, 1,
                                         &info, sizeof(info));
        before_prot = query_rc == 0 ? info.protection : -1;
        if (query_rc == 0 && !(info.protection & PROT_EXEC) &&
            !__atomic_exchange_n(&fault_page_rwx_tested, 1, __ATOMIC_RELAXED))
        {
            /* 0949 showed RX requested successfully while the kernel still
             * reported read-only. Test whether this direct-memory image page
             * needs W permission present when execute is granted. Keep the
             * experiment to the one 16 KiB page containing the fault. */
            rwx_rc = sceKernelMprotect((const void *)0x004a8000u, PAGE,
                                       PROT_READ | PROT_WRITE | PROT_EXEC);
            memset(&info, 0, sizeof(info));
            final_query_rc = sceKernelVirtualQuery((const void *)0x004a8330u, 1,
                                                   &info, sizeof(info));
        }
        else
        {
            final_query_rc = query_rc;
        }
        if (query_rc == 0) info.name[sizeof(info.name) - 1] = '\0';
        {
            char line[256];
            snprintf(line, sizeof(line),
                     "wine-ps4: fault-page query addr=0x004a8330 mprotect=%d/%d query=%d before=%#x rwx=%d final=%d range=%p-%p prot=%#x type=%#x flags=%#x name=%s\n",
                     kernel_rc, libc_rc, query_rc, before_prot, rwx_rc,
                     final_query_rc, (void *)info.start, (void *)info.end,
                     info.protection, info.memory_type, info.flags,
                     final_query_rc == 0 ? info.name : "?");
            sceKernelDebugOutText(0, line);
        }
    }

    if ((uintptr_t)address < 0x100000000ULL && __atomic_fetch_add(&logged, 1, __ATOMIC_RELAXED) < 32)
    {
        char line[192];
        snprintf(line, sizeof(line),
                 "wine-ps4: exec mprotect addr=%p bytes=%zu prot=%#x sce=%d libc=%d\n",
                 address, bytes, protection, kernel_rc, libc_rc);
        /* stderr is not captured on this title (dup2 onto fd 1/2 returns
         * EPERM), so send this bounded result through the live klog stream. */
        sceKernelDebugOutText(0, line);
    }
    return kernel_rc ? libc_rc : 0;
}

static int reserve(void *context, uintptr_t address, size_t bytes)
{
    void *at = (void *)address;
    uint64_t t0 = slow_start();
    int rc;
    (void)context;
    rc = sceKernelReserveVirtualRange(&at, bytes, KERNEL_FIXED | KERNEL_NO_OVERWRITE, PAGE) ||
         at != (void *)address;
    slow_end(t0, "reserve", bytes);
    return rc;
}
static int allocate(void *context, size_t bytes, int64_t *offset)
{
    uint64_t t0 = slow_start();
    int rc;
    (void)context;
    rc = sceKernelAllocateMainDirectMemory(bytes, PAGE, MEMORY_TYPE, offset) != 0;
    slow_end(t0, "allocate", bytes);
    return rc;
}
static int map(void *context, uintptr_t address, size_t bytes, int64_t offset)
{
    void *at = (void *)address;
    uint64_t t0 = slow_start();
    int rc;
    (void)context;
    rc = sceKernelMapDirectMemory(&at, bytes, map_protection, KERNEL_FIXED, offset, PAGE);
    slow_end(t0, "map", bytes);
    if (rc && map_protection == CPU_READ_WRITE) {
        /* Refused CPU-only: the GPU bits, and keep them if they work. */
        at = (void *)address;
        rc = sceKernelMapDirectMemory(&at, bytes, CPU_GPU_READ_WRITE, KERNEL_FIXED, offset, PAGE);
        if (!rc) map_protection = CPU_GPU_READ_WRITE;
    }
    return rc || at != (void *)address;
}
static int protect(void *context, uintptr_t address, size_t bytes, unsigned protection)
{
    uint64_t t0 = slow_start();
    int rc;
    (void)context;
    rc = ps4_host_mprotect((void *)address, bytes, (int)protection) != 0;
    slow_end(t0, "protect", bytes);
    return rc;
}
static int unmap(void *context, uintptr_t address, size_t bytes)
{
    uint64_t t0 = slow_start();
    int rc;
    (void)context;
    rc = sceKernelMunmap((void *)address, bytes) != 0;
    slow_end(t0, "unmap", bytes);
    return rc;
}
static int release(void *context, int64_t offset, size_t bytes)
{
    uint64_t t0 = slow_start();
    int rc;
    (void)context;
    rc = sceKernelReleaseDirectMemory(offset, bytes) != 0;
    slow_end(t0, "release", bytes);
    return rc;
}

static const PwWineDmemOps ops = { NULL, PAGE, reserve, allocate, map, protect, unmap, release };
static PwWineDmem dmem;
static PwWineDmemRun runs[RUNS];
/* 0 before the self-check, 1 in use, -1 refused: every call passes. */
static int state;

/* Wine's Unix heap also consumes flexible memory, including allocations made
 * by Mesa. Keep at least 64 MiB of the largest direct-memory block available
 * for ntdll and other runtime users. If the query or allocation fails, the
 * generic heap falls back to its existing anonymous mmap path. */
enum { HEAP_SLOTS = 1 << 17, HEAP_DIRECT_HEADROOM = 64 * 1024 * 1024 };
typedef struct HeapBlock { uintptr_t address; size_t bytes; int64_t offset; } HeapBlock;
static HeapBlock heap_blocks[HEAP_SLOTS];
static unsigned heap_block_count;
static int heap_direct_fallback_logged;

static size_t heap_slot(uintptr_t address)
{
    return (size_t)((address / PAGE) * UINT64_C(0x9e3779b97f4a7c15) >> 47) & (HEAP_SLOTS - 1);
}

static void heap_log_fallback(size_t bytes, size_t available, const char *why)
{
    if (__atomic_exchange_n(&heap_direct_fallback_logged, 1, __ATOMIC_RELAXED)) return;
    char line[192];
    snprintf(line, sizeof(line),
             "wine-ps4: direct heap fallback reason=%s request=%zu available=%zu reserve=%u\n",
             why, bytes, available, (unsigned)HEAP_DIRECT_HEADROOM);
    sceKernelDebugOutText(0, line);
}

static void *heap_map(void *context, size_t bytes)
{
    size_t span, available = 0, slot;
    off_t phys = 0;
    int64_t offset;
    void *at = NULL;
    (void)context;

    if (getenv("WINE_PS5_HEAP_FLEXIBLE")) return NULL;
    if (bytes > SIZE_MAX - (PAGE - 1)) return NULL;
    span = (bytes + PAGE - 1) & ~(size_t)(PAGE - 1);
    if (heap_block_count >= HEAP_SLOTS / 2) {
        heap_log_fallback(span, available, "table-full");
        return NULL;
    }
    if (sceKernelAvailableDirectMemorySize(0, (off_t)sceKernelGetDirectMemorySize(),
                                          PAGE, &phys, &available) ||
        available < span || available - span < HEAP_DIRECT_HEADROOM) {
        heap_log_fallback(span, available, "headroom");
        return NULL;
    }
    if (allocate(NULL, span, &offset)) {
        heap_log_fallback(span, available, "allocate");
        return NULL;
    }
    if (sceKernelMapDirectMemory(&at, span, map_protection, 0, offset, PAGE) || !at) {
        (void)release(NULL, offset, span);
        heap_log_fallback(span, available, "map");
        return NULL;
    }
    for (slot = heap_slot((uintptr_t)at); heap_blocks[slot].address;
         slot = (slot + 1) & (HEAP_SLOTS - 1)) {}
    heap_blocks[slot] = (HeapBlock){ (uintptr_t)at, span, offset };
    heap_block_count++;
    return at;
}

static int heap_unmap(void *context, void *address, size_t bytes)
{
    size_t slot, hole, next;
    (void)context;
    (void)bytes;
    for (slot = heap_slot((uintptr_t)address); heap_blocks[slot].address;
         slot = (slot + 1) & (HEAP_SLOTS - 1)) {
        HeapBlock block = heap_blocks[slot];
        if (block.address != (uintptr_t)address) continue;
        (void)sceKernelMunmap((void *)block.address, block.bytes);
        (void)sceKernelReleaseDirectMemory(block.offset, block.bytes);
        heap_block_count--;
        heap_blocks[slot].address = 0;
        for (hole = slot, next = (slot + 1) & (HEAP_SLOTS - 1);
             heap_blocks[next].address; next = (next + 1) & (HEAP_SLOTS - 1)) {
            size_t home = heap_slot(heap_blocks[next].address);
            if (((next - home) & (HEAP_SLOTS - 1)) >= ((next - hole) & (HEAP_SLOTS - 1))) {
                heap_blocks[hole] = heap_blocks[next];
                heap_blocks[next].address = 0;
                hole = next;
            }
        }
        return 0;
    }
    return 1;
}

static void heap_use_direct_memory(void)
{
    static const PwWineHeapBacking heap_backing = { NULL, heap_map, heap_unmap };
    if (!getenv("WINE_PS5_HEAP_FLEXIBLE")) {
        pw_wine_heap_set_backing(&heap_backing);
        sceKernelDebugOutText(0, "wine-ps4: Unix heap prefers direct memory; 64 MiB headroom guard active\n");
    }
}

/* A direct-memory RX request can return success without changing the effective
 * permission. Move that allocation granule to flexible memory, preserving its
 * contents, only when the kernel query confirms RX did not take. */
static int migrate_exec_page_to_flex(uintptr_t page)
{
    uint8_t saved[PAGE];
    void *mapped;
    int release_rc, adopt_rc;
    char line[192];

    if (pw_wine_dmem_backed(&dmem, page, PAGE) != PAGE) return 0;

    memcpy(saved, (const void *)page, sizeof(saved));
    release_rc = pw_wine_dmem_replace(&dmem, page, PAGE, 0);
    if (release_rc)
    {
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-page flex-migrate release failed rc=%d\n", release_rc);
        sceKernelDebugOutText(0, line);
        return -1;
    }

    mapped = mmap((void *)page, PAGE, PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED || mapped != (void *)page)
    {
        int map_errno = errno;
        if (mapped != MAP_FAILED) (void)munmap(mapped, PAGE);
        if (!pw_wine_dmem_replace(&dmem, page, PAGE, PROT_READ | PROT_WRITE))
            memcpy((void *)page, saved, sizeof(saved));
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-page flex-migrate mmap failed addr=%p errno=%d\n",
                 mapped, map_errno);
        sceKernelDebugOutText(0, line);
        return -1;
    }

    memcpy(mapped, saved, sizeof(saved));
    adopt_rc = pw_wine_dmem_adopt(&dmem, page, PAGE);
    if (adopt_rc)
    {
        (void)munmap(mapped, PAGE);
        if (!pw_wine_dmem_replace(&dmem, page, PAGE, PROT_READ | PROT_WRITE))
            memcpy((void *)page, saved, sizeof(saved));
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-page flex-migrate adopt failed rc=%d\n", adopt_rc);
        sceKernelDebugOutText(0, line);
        return -1;
    }

    snprintf(line, sizeof(line),
             "wine-ps4: exec-page flex-migrate complete addr=%p bytes=%u\n",
             (void *)page, (unsigned)PAGE);
    sceKernelDebugOutText(0, line);
    return 0;
}

/* Move a contiguous direct-memory span to flexible memory in one operation.
 * The per-page version above is retained only for mixed/sparse mappings where
 * a queried range is not wholly backed by direct memory. */
static int migrate_exec_range_to_flex(uintptr_t address, size_t bytes)
{
    void *saved, *mapped;
    int release_rc, adopt_rc;
    char line[192];

    if (!bytes || address % PAGE || bytes % PAGE ||
        pw_wine_dmem_backed(&dmem, address, bytes) != bytes)
        return -1;

    saved = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANON, -1, 0);
    if (saved == MAP_FAILED)
    {
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-range flex-migrate scratch failed bytes=%zu errno=%d\n",
                 bytes, errno);
        sceKernelDebugOutText(0, line);
        return -1;
    }
    memcpy(saved, (const void *)address, bytes);

    release_rc = pw_wine_dmem_replace(&dmem, address, bytes, 0);
    if (release_rc)
    {
        munmap(saved, bytes);
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-range flex-migrate release failed addr=%p bytes=%zu rc=%d\n",
                 (void *)address, bytes, release_rc);
        sceKernelDebugOutText(0, line);
        return -1;
    }

    mapped = mmap((void *)address, bytes, PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED || mapped != (void *)address)
    {
        int map_errno = errno, restore_rc;
        if (mapped != MAP_FAILED) (void)munmap(mapped, bytes);
        /* ⚠ restore_rc was discarded before. A hole in committed memory only
         * persists when this restore fails, so it is the one value that says
         * whether a failed migration left damage behind. */
        restore_rc = pw_wine_dmem_replace(&dmem, address, bytes,
                                         PROT_READ | PROT_WRITE);
        if (!restore_rc) memcpy((void *)address, saved, bytes);
        munmap(saved, bytes);
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-range flex-migrate map failed addr=%p bytes=%zu errno=%d restore=%d tid=%lu\n",
                 mapped, bytes, map_errno, restore_rc, glue_tid());
        sceKernelDebugOutText(0, line);
        return -1;
    }

    memcpy(mapped, saved, bytes);
    adopt_rc = pw_wine_dmem_adopt(&dmem, address, bytes);
    if (adopt_rc)
    {
        (void)munmap(mapped, bytes);
        if (!pw_wine_dmem_replace(&dmem, address, bytes,
                                  PROT_READ | PROT_WRITE))
            memcpy((void *)address, saved, bytes);
        munmap(saved, bytes);
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-range flex-migrate adopt failed addr=%p bytes=%zu rc=%d\n",
                 mapped, bytes, adopt_rc);
        sceKernelDebugOutText(0, line);
        return -1;
    }
    munmap(saved, bytes);

    snprintf(line, sizeof(line),
             "wine-ps4: exec-range flex-migrate complete addr=%p bytes=%zu tid=%lu\n",
             (void *)address, bytes, glue_tid());
    sceKernelDebugOutText(0, line);
    return 0;
}

/* Apply RX at the PS4 direct-memory granule, then query what the kernel
 * actually installed. If a fully direct-backed page remains non-executable,
 * migrate it to flexible memory and retry. */
static int protect_exec_page(unsigned int call, uintptr_t page, int protection)
{
    PwPs4VirtualQueryInfo info;
    int protect_rc, query_rc, migrate_rc = -2, retry_rc = -2;
    int host_retry_rc = -2;
    int effective = -1;
    size_t dmem_bytes;
    char line[256];

    /* The native 32-bit guest image lives below 4 GiB. Higher addresses are
     * Wine's 64-bit host runtime, including stacks; keep their existing
     * protection path and never replace those mappings with flexible memory. */
    if (page >= 0x100000000ULL)
        return pw_wine_dmem_protect(&dmem, page, PAGE,
                                    (unsigned)protection & 7);

    protect_rc = pw_wine_dmem_protect(&dmem, page, PAGE,
                                      (unsigned)protection & 7);
    memset(&info, 0, sizeof(info));
    query_rc = sceKernelVirtualQuery((const void *)page, 1, &info, sizeof(info));
    if (!query_rc) effective = info.protection;
    dmem_bytes = pw_wine_dmem_backed(&dmem, page, PAGE);

    /* A caller-backed mapping can report success from libkernel while the
     * effective protection still omits execute. In that case retry through
     * libc's mprotect wrapper once and verify the kernel's final mapping. */
    if (!query_rc && !(effective & PROT_EXEC) && !dmem_bytes)
    {
        host_retry_rc = mprotect((void *)page, PAGE, protection);
        memset(&info, 0, sizeof(info));
        query_rc = sceKernelVirtualQuery((const void *)page, 1, &info, sizeof(info));
        effective = query_rc ? -1 : info.protection;
    }

    if (!query_rc && !(effective & PROT_EXEC) && dmem_bytes == PAGE)
    {
        migrate_rc = migrate_exec_page_to_flex(page);
        if (!migrate_rc)
        {
            retry_rc = pw_wine_dmem_protect(&dmem, page, PAGE,
                                            (unsigned)protection & 7);
            memset(&info, 0, sizeof(info));
            query_rc = sceKernelVirtualQuery((const void *)page, 1,
                                             &info, sizeof(info));
            if (!query_rc) effective = info.protection;
        }
    }

    if (protect_rc || query_rc || !(effective & PROT_EXEC) || migrate_rc != -2 ||
        host_retry_rc != -2)
    {
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-protect-verify build=0975 call=%u page=%p dmem-before=%zu protect=%d query=%d effective=%#x host-retry=%d migrate=%d retry=%d\n",
                 call, (void *)page, dmem_bytes, protect_rc, query_rc,
                 effective, host_retry_rc, migrate_rc, retry_rc);
        sceKernelDebugOutText(0, line);
    }

    if ((protect_rc && retry_rc) || query_rc || !(effective & PROT_EXEC))
        return -1;
    return 0;
}

/* A single executable mprotect can cover a very large guest range (for
 * example, a reservation with sparse committed pages).  Migrating every
 * direct-memory granule in that request is both slow and can consume the
 * title's flexible-memory pool.  Keep the fallback bounded to ordinary
 * image-sized requests; larger ranges keep the original dmem protection
 * behavior and are logged for diagnosis. */
/* Bound on a single exec-range migration. 0 disables exec migration entirely:
 * exec_migration_allowed goes false for every nonzero request, so
 * protect_exec_range() (and the per-page fallback inside it) never runs and
 * PROT_EXEC on dmem-backed memory falls back to plain pw_wine_dmem_protect().
 * Overridable at compile time for scoped A/B tests via PW_PRX_EXTRA_CFLAGS. */
#ifndef PW_PS4_EXEC_MIGRATE_MAX_BYTES
#define PW_PS4_EXEC_MIGRATE_MAX_BYTES (16u * 1024u * 1024u)
#endif

/* 1 forces the per-page migration loop (protect_exec_page, one PAGE at a time)
 * instead of migrating a whole dmem-backed span in one go. Same end state, but
 * the window in which the range is unmapped shrinks from up to 16 MiB to 16 KiB.
 * migrate_exec_range_to_flex() snapshots, unmaps, re-mmaps and copies back with
 * no lock held, so a concurrent write inside that window is silently reverted;
 * this bounds the exposure. Scoped A/B via PW_PRX_EXTRA_CFLAGS. */
#ifndef PW_PS4_EXEC_MIGRATE_PER_PAGE_ONLY
#define PW_PS4_EXEC_MIGRATE_PER_PAGE_ONLY 0
#endif

static uintptr_t page_down(const void *address) { return (uintptr_t)address & ~(uintptr_t)(PAGE - 1); }
static size_t page_span(const void *address, size_t bytes)
{
    return (((uintptr_t)address + bytes + PAGE - 1) & ~(uintptr_t)(PAGE - 1)) - page_down(address);
}

/* Protect one contiguous image range, querying mapping boundaries instead of
 * probing every 16 KiB page. A fully direct-backed, non-executable mapping is
 * moved as a range; only mixed/sparse mappings fall back to page granularity. */
static int protect_exec_range(unsigned int call, uintptr_t address, size_t bytes,
                              int protection)
{
    uintptr_t at = address, end = address + bytes;
    char line[224];

    if (address % PAGE || bytes % PAGE || end < address) return -1;
    if (pw_wine_dmem_protect(&dmem, address, bytes,
                             (unsigned)protection & 7))
    {
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-range batch-protect failed call=%u addr=%p bytes=%zu\n",
                 call, (void *)address, bytes);
        sceKernelDebugOutText(0, line);
    }

    while (at < end)
    {
        PwPs4VirtualQueryInfo info;
        uintptr_t next;
        size_t span, backed;
        int query_rc, effective = -1;

        memset(&info, 0, sizeof(info));
        query_rc = sceKernelVirtualQuery((const void *)at, 1, &info, sizeof(info));
        if (!query_rc) effective = info.protection;
        next = !query_rc && info.end > at ? info.end : at + PAGE;
        if (next > end) next = end;
        if (next <= at) return -1;
        span = next - at;

        if (!query_rc && (effective & PROT_EXEC))
        {
            at = next;
            continue;
        }

        backed = pw_wine_dmem_backed(&dmem, at, span);
        if (!query_rc && backed == span && !PW_PS4_EXEC_MIGRATE_PER_PAGE_ONLY)
        {
            int migrate_rc = migrate_exec_range_to_flex(at, span);
            int retry_rc = migrate_rc ? -1 :
                pw_wine_dmem_protect(&dmem, at, span,
                                     (unsigned)protection & 7);

            memset(&info, 0, sizeof(info));
            query_rc = sceKernelVirtualQuery((const void *)at, 1,
                                             &info, sizeof(info));
            effective = query_rc ? -1 : info.protection;
            snprintf(line, sizeof(line),
                     "wine-ps4: exec-range verify call=%u addr=%p bytes=%zu migrate=%d protect=%d query=%d effective=%#x tid=%lu\n",
                     call, (void *)at, span, migrate_rc, retry_rc,
                     query_rc, effective, glue_tid());
            sceKernelDebugOutText(0, line);
            if (migrate_rc || retry_rc || query_rc || !(effective & PROT_EXEC))
                return -1;
            at = next;
            continue;
        }

        /* A query failure or a mixture of direct and caller-backed pages
         * cannot safely be replaced as one allocation. Preserve the proven
         * per-page behavior only for this exceptional case. */
        for (uintptr_t page = at; page < next; page += PAGE)
            if (protect_exec_page(call, page, protection)) return -1;
        at = next;
    }
    return 0;
}

/* One page through every step, inside the first region, before any use. */
static int self_check(uintptr_t address)
{
    volatile uint8_t *bytes = (volatile uint8_t *)address;
    int ok = !pw_wine_dmem_replace(&dmem, address, PAGE, PW_WINE_DMEM_READ | PW_WINE_DMEM_WRITE);

    if (ok) {
        PwPs4VirtualQueryInfo info;
        int protect_rc, query_rc;
        char line[256];

        bytes[0] = 0x5a;
        bytes[PAGE - 1] = 0xa5;
        protect_rc = pw_wine_dmem_protect(&dmem, address, PAGE,
                                          PW_WINE_DMEM_READ | PW_WINE_DMEM_EXEC);
        memset(&info, 0, sizeof(info));
        query_rc = sceKernelVirtualQuery((const void *)address, 1, &info, sizeof(info));
        if (!query_rc) info.name[sizeof(info.name) - 1] = '\0';
        snprintf(line, sizeof(line),
                 "wine-ps4: dmem-exec-selfcheck build=0971 addr=%p mprotect=%d query=%d range=%p-%p prot=%#x type=%#x flags=%#x\n",
                 (void *)address, protect_rc, query_rc, (void *)info.start,
                 (void *)info.end, info.protection, info.memory_type, info.flags);
        sceKernelDebugOutText(0, line);
        ok = !protect_rc && bytes[0] == 0x5a && bytes[PAGE - 1] == 0xa5 &&
             !pw_wine_dmem_protect(&dmem, address, PAGE,
                                   PW_WINE_DMEM_READ | PW_WINE_DMEM_WRITE);
    }
    return !pw_wine_dmem_replace(&dmem, address, PAGE, 0) && ok;
}

/* Wine's PE metadata says RX for this image, but the kernel reports the
 * instruction-fetch pages as read-only. Record the effective mapping directly
 * after each executable protection operation, including whether Wine routed
 * the page through its direct-memory allocator. */
static void trace_oblivion_rx_result(unsigned int call, uintptr_t caller,
                                     uintptr_t address, size_t bytes,
                                     int protection, const char *route, int rc)
{
    static unsigned logs;
    static const uintptr_t samples[] = { 0x0049e000u, 0x004a6000u, 0x004a8000u };
    uintptr_t start = (uintptr_t)address, end = start + bytes;

    if (!(protection & PROT_EXEC) || start >= 0x004ac000u || end <= 0x00400000u ||
        __atomic_fetch_add(&logs, 1, __ATOMIC_RELAXED) >= 24) return;

    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
        PwPs4VirtualQueryInfo info;
        uintptr_t sample = samples[i];
        int query_rc;
        char line[256];

        if (sample < start || sample >= end) continue;
        memset(&info, 0, sizeof(info));
        query_rc = sceKernelVirtualQuery((const void *)sample, 1, &info, sizeof(info));
        if (!query_rc) info.name[sizeof(info.name) - 1] = '\0';
        snprintf(line, sizeof(line),
                 "wine-ps4: image-rx-result build=0971 call=%u caller=%p request=%p+%zu prot=%#x route=%s rc=%d sample=%p dmem=%zu query=%d mapping=%p-%p effective=%#x type=%#x flags=%#x\n",
                 call, (void *)caller, (void *)address, bytes, protection, route, rc,
                 (void *)sample, pw_wine_dmem_backed(&dmem, sample, PAGE),
                 query_rc, (void *)info.start, (void *)info.end,
                 info.protection, info.memory_type, info.flags);
        sceKernelDebugOutText(0, line);
    }
}

/* Observe, but never repair, the affected image pages on the existing Wine
 * memory-stat heartbeat. This distinguishes a page that never becomes RX from
 * one whose effective protection changes between samples. */
static void watch_oblivion_rx_pages(unsigned int sample)
{
    static const uintptr_t pages[] = { 0x0049e000u, 0x004a6000u, 0x004a8000u };
    static int initialized[sizeof(pages) / sizeof(pages[0])];
    static int last_query[sizeof(pages) / sizeof(pages[0])];
    static int last_protection[sizeof(pages) / sizeof(pages[0])];
    static uintptr_t last_start[sizeof(pages) / sizeof(pages[0])];
    static uintptr_t last_end[sizeof(pages) / sizeof(pages[0])];

    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
    {
        PwPs4VirtualQueryInfo info;
        int query_rc;
        char line[256];

        memset(&info, 0, sizeof(info));
        query_rc = sceKernelVirtualQuery((const void *)pages[i], 1, &info, sizeof(info));
        if (!query_rc) info.name[sizeof(info.name) - 1] = '\0';
        if (initialized[i] && query_rc == last_query[i] &&
            (query_rc || (info.protection == last_protection[i] &&
                          info.start == last_start[i] && info.end == last_end[i])))
            continue;

        snprintf(line, sizeof(line),
                 "wine-ps4: image-rx-watch build=0971 sample=%u page=%p dmem=%zu query=%d mapping=%p-%p effective=%#x type=%#x flags=%#x\n",
                 sample, (void *)pages[i], pw_wine_dmem_backed(&dmem, pages[i], PAGE),
                 query_rc, (void *)info.start, (void *)info.end,
                 info.protection, info.memory_type, info.flags);
        sceKernelDebugOutText(0, line);
        initialized[i] = 1;
        last_query[i] = query_rc;
        last_protection[i] = info.protection;
        last_start[i] = info.start;
        last_end[i] = info.end;
    }
}

/* ntdll's reserved area [base, +size), already reserved: backed by direct
 * memory from now on. ntdll also gives each reservation it makes at a fixed
 * address outside those areas: the console does not back a bare reservation
 * when it is protected, so committing one would leave it without memory.
 * Such a region is dropped again when it is unmapped. */
void __wine_ps5_dmem_region(void *base, size_t size)
{
    if (!state && pw_wine_dmem_init(&dmem, &ops, runs, RUNS, 1)) state = -1;
    if (state < 0) return;
    if (pw_wine_dmem_add_region(&dmem, (uintptr_t)base, size, 1)) {
        fprintf(stderr, "wine-ps5: no direct memory region for %p, %zu bytes\n", base, size);
        return;
    }
    if (!state) {
        state = self_check((uintptr_t)base) ? 1 : -1;
        fprintf(stderr, "wine-ps5: direct memory %s for anonymous memory (region %p, %zu MiB)\n",
                state > 0 ? "in use" : "refused", base, size >> 20);
        if (state > 0) heap_use_direct_memory();
    }
}

void *__wine_ps5_mmap(void *address, size_t bytes, int protection, int flags, int fd, off_t offset)
{
    uintptr_t at = page_down(address), end = at + page_span(address, bytes);
    size_t piece;
    int status = 0;

    if (state <= 0 || !(flags & MAP_FIXED) || (uintptr_t)address != at ||
        (!pw_wine_dmem_split(&dmem, at, end - at, &piece) && piece == end - at))
        return mmap(address, bytes, protection, flags, fd, offset);
    if (!(flags & MAP_ANON)) {
        void *mapped;

        /* A private file view (an image section) is read into direct
         * memory instead: ntdll falls back to pread when mmap reports
         * ENODEV. On the console a fixed file mapping over a reservation
         * does not give the requested pages. */
        if (!(flags & MAP_SHARED)) {
            errno = ENODEV;
            return MAP_FAILED;
        }
        /* Shared memory stays a file mapping, over what is there, as
         * before direct memory; only the exact address is accepted. */
        mapped = mmap(address, bytes, protection, flags, fd, offset);
        if (mapped == MAP_FAILED) return MAP_FAILED;
        if (mapped != address) {
            munmap(mapped, bytes);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        for (; at < end; at += piece)
            if (pw_wine_dmem_split(&dmem, at, end - at, &piece)) (void)pw_wine_dmem_adopt(&dmem, at, piece);
        return address;
    }
    for (; at < end; at += piece)
        if (pw_wine_dmem_split(&dmem, at, end - at, &piece))
            status |= pw_wine_dmem_replace(&dmem, at, piece, (unsigned)protection & 7);
        else if (mmap((void *)at, piece, protection, flags, -1, 0) == MAP_FAILED)
            status = -1;
    if (!status) return address;
    errno = ENOMEM;
    return MAP_FAILED;
}

/* Owned pages give their direct memory back, then their address space. */
int __wine_ps5_munmap(void *address, size_t bytes)
{
    uintptr_t at = page_down(address), end = at + page_span(address, bytes);
    size_t piece;
    int status = 0;

    if (state <= 0) return munmap(address, bytes);
    for (; at < end; at += piece)
        if (pw_wine_dmem_split(&dmem, at, end - at, &piece))
            status |= pw_wine_dmem_replace(&dmem, at, piece, 0) | sceKernelMunmap((void *)at, piece);
        else
            status |= munmap((void *)at, piece);
    pw_wine_dmem_remove_regions(&dmem, page_down(address), page_span(address, bytes));
    if (status) errno = EINVAL;
    return status ? -1 : 0;
}

int __wine_ps5_mprotect(void *address, size_t bytes, int protection)
{
    static unsigned trace_calls;
    static unsigned trace_fault_page_calls;
    static unsigned exec_range_logs;
    uintptr_t at = page_down(address), end = at + page_span(address, bytes);
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    size_t piece;
    int status = 0;
    unsigned call = __atomic_fetch_add(&trace_calls, 1, __ATOMIC_RELAXED) + 1;
    uintptr_t start = (uintptr_t)address;
    int trace_force_exec = start < 0x1000110000ULL && end > 0x1000000000ULL;
    int fault_page = start <= 0x004a8330u && bytes > 0x004a8330u - start;
    int trace = call <= 16 || trace_force_exec;
    int exec_migration_allowed = bytes <= PW_PS4_EXEC_MIGRATE_MAX_BYTES;

    if ((protection & PROT_EXEC) && start < 0x100000000ULL &&
        (bytes >= 1024u * 1024u || call <= 16) &&
        __atomic_fetch_add(&exec_range_logs, 1, __ATOMIC_RELAXED) < 64)
    {
        char line[256];
        snprintf(line, sizeof(line),
                 "wine-ps4: exec-range build=0974 call=%u addr=%p bytes=%zu prot=%#x caller=%p fallback=%s\n",
                 call, address, bytes, protection, (void *)caller,
                 exec_migration_allowed ? "bounded" : "skip-large");
        sceKernelDebugOutText(0, line);
    }

    if (fault_page && __atomic_fetch_add(&trace_fault_page_calls, 1, __ATOMIC_RELAXED) < 32)
        trace = 1;
    if (trace)
    {
        char line[224];
        snprintf(line, sizeof(line),
                 "wine-ps4: mprotect-trace build=0971 enter call=%u addr=%p bytes=%zu prot=%#x state=%d fault_page=%d\n",
                 call, address, bytes, protection, state, fault_page);
        sceKernelDebugOutText(0, line);
    }

    /* Keep the original target-range dispatch marker alongside the build ID. */
    if (fault_page)
    {
        char line[192];
        snprintf(line, sizeof(line),
                 "wine-ps4: fault mprotect dispatch addr=%p bytes=%zu prot=%#x state=%d\n",
                 address, bytes, protection, state);
        sceKernelDebugOutText(0, line);
    }

    if (state <= 0)
    {
        status = ps4_host_mprotect(address, bytes, protection);
        trace_oblivion_rx_result(call, caller, (uintptr_t)address, bytes,
                                 protection, "host", status);
        if (trace)
        {
            char line[160];
            snprintf(line, sizeof(line),
                     "wine-ps4: mprotect-trace build=0971 leave call=%u rc=%d path=host\n",
                     call, status);
            sceKernelDebugOutText(0, line);
        }
        return status;
    }
    for (; at < end; at += piece)
    {
        int backed;
        if (trace_force_exec)
            force_exec_trace("wine-ps4: force-exec mprotect split begin addr=%p remaining=%zu prot=%#x\n",
                             (void *)at, (size_t)(end - at), protection);
        backed = pw_wine_dmem_split(&dmem, at, end - at, &piece);
        if (trace_force_exec)
            force_exec_trace("wine-ps4: force-exec mprotect split result addr=%p bytes=%zu backed=%d\n",
                             (void *)at, piece, backed);
        if (backed)
        {
            int rc = 0;

            if (trace_force_exec)
                force_exec_trace("wine-ps4: force-exec dmem protect begin addr=%p bytes=%zu prot=%#x\n",
                                 (void *)at, piece, protection);
            if ((protection & PROT_EXEC) && exec_migration_allowed)
                rc = protect_exec_range(call, at, piece, protection);
            else
                rc = pw_wine_dmem_protect(&dmem, at, piece,
                                          (unsigned)protection & 7);
            if (trace_force_exec)
                force_exec_trace("wine-ps4: force-exec dmem protect result addr=%p bytes=%zu rc=%d\n",
                                 (void *)at, piece, rc);
            trace_oblivion_rx_result(call, caller, at, piece, protection, "dmem", rc);
            status |= rc;
        }
        else
        {
            if (trace_force_exec)
                force_exec_trace("wine-ps4: force-exec host protect begin addr=%p bytes=%zu prot=%#x\n",
                                 (void *)at, piece, protection);
            int rc = ps4_host_mprotect((void *)at, piece, protection);
            if (trace_force_exec)
                force_exec_trace("wine-ps4: force-exec host protect result addr=%p bytes=%zu rc=%d\n",
                                 (void *)at, piece, rc);
            trace_oblivion_rx_result(call, caller, at, piece, protection, "host", rc);
            status |= rc;
        }
    }
    if (status) errno = ENOMEM;
    status = status ? -1 : 0;
    if (trace)
    {
        char line[160];
        snprintf(line, sizeof(line),
                 "wine-ps4: mprotect-trace build=0971 leave call=%u rc=%d path=dmem\n",
                 call, status);
        sceKernelDebugOutText(0, line);
    }
    return status;
}

/* For the title's log, as __wine_virtual_stats: direct memory backed now and
 * at most, its runs and refused calls, the heap's mapped and peak mapped
 * bytes, then the direct memory backed below 4 GiB now and at most. Returns
 * how many counters there are. */
unsigned int __wine_ps5_memory_stats(uint64_t *out, unsigned int count)
{
    static unsigned int page_watch_sample, page_watch_logs;
    static int page_watch_initialized, page_watch_query, page_watch_protection;
    static uintptr_t page_watch_start, page_watch_end;
    PwWineDmemStats dmem_stats = { 0 };
    PwWineHeapStats heap_stats;
    PwWineHeapFailures heap_failures = { 0 };
    uint64_t values[14];

    if (state > 0) pw_wine_dmem_stats(&dmem, &dmem_stats);
    pw_wine_heap_stats(&heap_stats);
    pw_wine_heap_failures(&heap_failures);
    values[0] = dmem_stats.backed_bytes;
    values[1] = dmem_stats.peak_backed_bytes;
    values[2] = dmem_stats.runs;
    values[3] = dmem_stats.failures;
    values[4] = heap_stats.mapped_bytes;
    values[5] = heap_stats.peak_mapped_bytes;
    values[6] = dmem_stats.low_backed_bytes;
    values[7] = dmem_stats.peak_low_backed_bytes;
    values[8] = heap_stats.failures;
    values[9] = heap_failures.last_failed_bytes;
    values[10] = heap_failures.last_failure_reason;
    values[11] = heap_failures.backing_refusals;
    values[12] = heap_failures.last_map_bytes;
    values[13] = heap_failures.last_map_errno;
    for (unsigned int i = 0; i < count && i < 14; i++) out[i] = values[i];

    watch_oblivion_rx_pages(++page_watch_sample);

    /* The 0967 run found the page containing Oblivion's entry path already
     * read-only at the first sample, although Wine's PE metadata requests RX.
     * Repair only that exact 16-KiB anonymous mapping from this existing
     * heartbeat. This also distinguishes a false-success kernel mprotect from
     * an effective protection update without another thread or per-frame
     * queries. */
    {
        static unsigned int repair_attempts;
        PwPs4VirtualQueryInfo info;
        unsigned int sample = page_watch_sample;
        int query, protection = -1;
        uintptr_t start = 0, end = 0;

        memset(&info, 0, sizeof(info));
        query = sceKernelVirtualQuery((const void *)0x004a8330u, 1, &info, sizeof(info));
        if (!query)
        {
            protection = info.protection;
            start = info.start;
            end = info.end;
            info.name[sizeof(info.name) - 1] = '\0';
        }

        if (!query && start == 0x004a8000u && end == 0x004ac000u &&
            (protection & PROT_READ) && !(protection & PROT_EXEC) &&
            repair_attempts < 8)
        {
            int attempt = ++repair_attempts;
            int before = protection;
            int sce_rc, libc_rc = -999, verify_rc;
            int verify_protection = -1;
            char line[256];

            sce_rc = sceKernelMprotect((const void *)0x004a8000u, PAGE,
                                       PROT_READ | PROT_EXEC);
            memset(&info, 0, sizeof(info));
            verify_rc = sceKernelVirtualQuery((const void *)0x004a8330u, 1,
                                              &info, sizeof(info));
            if (!verify_rc)
            {
                info.name[sizeof(info.name) - 1] = '\0';
                verify_protection = info.protection;
            }
            if (verify_rc || !(verify_protection & PROT_EXEC))
            {
                libc_rc = mprotect((void *)0x004a8000u, PAGE,
                                   PROT_READ | PROT_EXEC);
                memset(&info, 0, sizeof(info));
                verify_rc = sceKernelVirtualQuery((const void *)0x004a8330u, 1,
                                                  &info, sizeof(info));
                if (!verify_rc)
                {
                    info.name[sizeof(info.name) - 1] = '\0';
                    verify_protection = info.protection;
                }
                else verify_protection = -1;
            }
            snprintf(line, sizeof(line),
                     "wine-ps4: fault-page-rx-repair build=0968 attempt=%u before=%#x sce=%d libc=%d verify=%d after=%#x\n",
                     attempt, before, sce_rc, libc_rc, verify_rc,
                     verify_protection);
            sceKernelDebugOutText(0, line);
            query = verify_rc;
            protection = verify_protection;
            if (!query)
            {
                start = info.start;
                end = info.end;
            }
        }
        if ((!page_watch_initialized || query != page_watch_query ||
             protection != page_watch_protection || start != page_watch_start ||
             end != page_watch_end) && page_watch_logs < 32)
        {
            char line[256];
            snprintf(line, sizeof(line),
                     "wine-ps4: fault-page-prot-watch build=0968 sample=%u query=%d range=%p-%p prot=%#x type=%#x flags=%#x name=%s\n",
                     sample, query, (void *)start, (void *)end, protection,
                     query ? 0 : info.memory_type, query ? 0 : info.flags,
                     query ? "?" : info.name);
            sceKernelDebugOutText(0, line);
            page_watch_logs++;
        }
        page_watch_initialized = 1;
        page_watch_query = query;
        page_watch_protection = protection;
        page_watch_start = start;
        page_watch_end = end;
    }
    return 14;
}
