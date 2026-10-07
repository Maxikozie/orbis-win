/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The PS4's filling of prospero-win's module-loading vtable.
 *
 * `PwWineDlOps` injects the three kernel calls so the dlopen/dlsym/dladdr logic
 * above them stays host-testable — upstream's words. All three exist on the PS4
 * with the same signatures, so this is four forwards.
 *
 * ⚠ AND THE MODULE-INFO LAYOUT IS IDENTICAL, which is the part that could
 * easily not have been. pw_prx_parse_module_info() hardcodes PS5 offsets: name
 * at 8, four segments at 0x108, their count at 0x148, 0x160 bytes in total.
 * Computed from the PS4 SDK's OrbisKernelModuleInfo:
 *
 *     size_t size              0x0000
 *     char name[256]           0x0008   <- matches
 *     segmentInfo[4]           0x0108   <- matches  (16 bytes each)
 *     uint32_t segmentCount    0x0148   <- matches
 *     uint8_t fingerprint[20]  0x014C
 *     total                    0x0160   <- matches PW_PRX_MODULE_INFO_BYTES
 *
 * So the parser is used unmodified. Had one field differed, every segment
 * address would have been read from the wrong place and the descriptor scan
 * would have failed in a way that looked like a missing descriptor.
 */
#include "pw_wine_dl_ps4.h"
#include <stdio.h>

#include <orbis/libkernel.h>

/* ⚠ THE SDK's OWN DECLARATIONS DIFFER FROM THE VTABLE's, in three ways that are
 * all const-ness or width rather than meaning:
 *
 *   LoadStartModule   returns uint32_t here, int32_t in the vtable; `option`
 *                     and `result` are plain void pointers, not const
 *   GetModuleInfo     takes OrbisKernelModuleInfo*, the vtable passes void*
 *   StopUnloadModule  `option` is void*, `result` is int32_t*
 *
 * Declaring our own externs to match the vtable collides with libkernel.h.
 * Casting at the forward is the honest fix: one place knows both shapes, and
 * the header stays the authority on what the kernel actually takes. */
static int32_t platform_load_start(const char *path, size_t argc, const void *argv,
                                   uint32_t flags, const void *option, int *result)
{
    return (int32_t)sceKernelLoadStartModule(path, argc, argv, flags,
                                             (void *)option, (void *)result);
}

static int platform_module_info(int32_t handle, void *info)
{
    return sceKernelGetModuleInfo((OrbisKernelModule)handle,
                                  (OrbisKernelModuleInfo *)info);
}

static int platform_stop_unload(int32_t handle, size_t argc, const void *argv,
                                uint32_t flags, const void *option, int *result)
{
    return sceKernelStopUnloadModule((OrbisKernelModule)handle, argc, argv, flags,
                                     (void *)option, (int32_t *)result);
}

/* Optional in the vtable, and worth supplying: without it an open that fails
 * says nothing about which paths were tried. */
static void (*g_report)(const char *) = NULL;

static void platform_report(const char *line)
{
    if (g_report) g_report(line);
    else fprintf(stderr, "%s\n", line);
}

int pw_wine_dl_ps4_ops(PwWineDlOps *ops, void (*report)(const char *))
{
    if (!ops) return PW_ERR_PRECONDITION;
    g_report = report;
    *ops = (PwWineDlOps){
        .load_start  = platform_load_start,
        .module_info = platform_module_info,
        .stop_unload = platform_stop_unload,
        .report      = platform_report,
    };
    return PW_OK;
}
