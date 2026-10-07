/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Direct memory behind Wine's anonymous mappings, on the PS4.
 *
 * The fifth vtable. `PwWineDmemOps` is six calls and a page size; everything
 * above it — owning address regions, backing pages on demand, tracking runs,
 * returning freed pages to reserved — is upstream's pw_wine_dmem.c, which
 * states outright that "the kernel is reached only through PwWineDmemOps, so
 * the policy is portable".
 *
 * ⚠ WHY ANY OF THIS EXISTS. A title's flexible memory (what an ordinary
 * anonymous mmap draws from) is a few hundred MiB; direct memory is the large
 * pool, allocated by physical offset and mapped where the caller chooses. Wine
 * wants far more anonymous memory than flexible memory has, so ntdll's VM
 * manager is routed here instead.
 *
 * ⚠ THE PS4 POOL IS MUCH SMALLER THAN THE PS5's. Upstream measured ~440 MiB
 * flexible and up to 12 GiB direct on FW 12.02. This console reported 661 MiB
 * host-visible and 1322 MiB device-local to Vulkan. The policy does not care —
 * it allocates on demand and releases eagerly — but the headroom Wine actually
 * gets here is a fraction of the PS5's, and that is a fact about this port
 * rather than a bug in it.
 */
#include "pw_wine_dmem_ps4.h"
#include <stddef.h>

#include <orbis/libkernel.h>

enum {
    PAGE = 0x4000,              /* 16 KiB, the direct-memory granule */
    KERNEL_FIXED = 0x10,        /* == MAP_FIXED in the SDK's mman.h */

    /* ⚠ UNCITED ON THE PS4. 0x80 is what upstream passes on the PS5 for
     * "do not replace an existing mapping", and the OpenOrbis headers define
     * no equivalent. Same kernel lineage, so the same value is the best
     * available guess — and the failure mode is benign: if the flag is
     * ignored, a reservation could silently replace something, which the
     * reserve/map self-check below would catch before any region is taken. */
    KERNEL_NO_OVERWRITE = 0x80,

    /* ⚠ NOT THE SCANOUT'S TYPE. pw_videoout_ps4.c uses WC_GARLIC (3) because
     * the compositor only ever writes a surface the display only ever reads.
     * This memory is Wine's general-purpose heap and stack: it is read far
     * more than written, so WB_ONION (0) — cached, CPU-coherent — is the one
     * that matters here. Using the scanout's type would work and be slow in a
     * way nothing would report. */
    MEMORY_TYPE = 0x0,          /* ORBIS_KERNEL_WB_ONION */

    CPU_READ_WRITE = 0x3,       /* ORBIS_KERNEL_PROT_CPU_RW */
};

static int reserve(void *context, uintptr_t address, size_t bytes)
{
    (void)context;
    void *at = (void *)address;
    return sceKernelReserveVirtualRange(&at, bytes,
                                        KERNEL_FIXED | KERNEL_NO_OVERWRITE, PAGE) != 0
           || at != (void *)address;
}

static int allocate(void *context, size_t bytes, int64_t *offset)
{
    (void)context;
    /* ⚠ NOT sceKernelAllocateMainDirectMemory. The PS4 SDK declares its last
     * parameter as `off_t` BY VALUE rather than `off_t *`, so the physical
     * offset cannot come back through it — a header bug that would produce a
     * garbage offset and a map that lands nowhere. sceKernelAllocateDirectMemory
     * has the correct out-pointer and takes an explicit [start, end) search
     * range, which the PS5's "main" variant does not. */
    off_t out = 0;
    size_t pool = sceKernelGetDirectMemorySize();
    if (sceKernelAllocateDirectMemory(0, (off_t)pool, bytes, PAGE, MEMORY_TYPE, &out) != 0)
        return 1;
    *offset = (int64_t)out;
    return 0;
}

static int map(void *context, uintptr_t address, size_t bytes, int64_t offset)
{
    (void)context;
    void *at = (void *)address;
    /* Mapped read-write and protected afterwards: execute is refused at map
     * time on both consoles and granted by mprotect, which is why the ops
     * split protect out as its own call. */
    return sceKernelMapDirectMemory(&at, bytes, CPU_READ_WRITE, KERNEL_FIXED,
                                    (off_t)offset, PAGE) != 0
           || at != (void *)address;
}

static int protect(void *context, uintptr_t address, size_t bytes, unsigned protection)
{
    (void)context;
    return sceKernelMprotect((const void *)address, bytes, (int)protection) != 0;
}

static int unmap(void *context, uintptr_t address, size_t bytes)
{
    (void)context;
    return sceKernelMunmap((void *)address, bytes) != 0;
}

static int release(void *context, int64_t offset, size_t bytes)
{
    (void)context;
    return sceKernelReleaseDirectMemory((off_t)offset, bytes) != 0;
}

int pw_wine_dmem_ps4_ops(PwWineDmemOps *ops)
{
    if (!ops) return PW_ERR_PRECONDITION;
    *ops = (PwWineDmemOps){
        .context  = NULL,
        .page     = PAGE,
        .reserve  = reserve,
        .allocate = allocate,
        .map      = map,
        .protect  = protect,
        .unmap    = unmap,
        .release  = release,
    };
    return PW_OK;
}
