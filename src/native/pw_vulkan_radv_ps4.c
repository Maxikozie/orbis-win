/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* libvulkan.prx's entry points on PS4.
 *
 * win32u reaches Vulkan by dlopen(SONAME_LIBVULKAN) -- "libvulkan.so" -- and
 * pw_wine_dl turns that into libvulkan.prx (pw_wine_dl.c:52). It then looks up
 * vkGetInstanceProcAddr and vkGetDeviceProcAddr. Mesa's archive exports
 * neither: it exports the ICD's door, vk_icdGetInstanceProcAddr, and the
 * common device dispatch, exactly as the bundle's vkloader documents. So this
 * file is the same two-symbol shim, which is all a loader on this console can
 * be -- there is no libvulkan.so to find and no icd.d to read.
 *
 * ⚠ This is the PS4 arm of wine/ps5/pw_vulkan_radv.c, not a copy of it. That
 * one also bridges presentation, forwarding pw_videoout_* to PS5_Mesa's
 * wsi_videoout_idle / _show_tiled / _present_rect. This Mesa has no such
 * symbols -- the PS4 build carries the stock Mesa WSI instead, 64 wsi_* entry
 * points including wsi_CreateSwapchainKHR -- so there is nothing to forward
 * to, and nothing imports pw_videoout_* here either (win32u.shared.elf names
 * none of them). Presentation is its own problem; this gets the driver
 * loadable, which is what every part of the graphics stack is waiting on. */
#include <vulkan/vulkan_core.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice device, const char *name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    return vk_icdGetInstanceProcAddr(instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    return vk_common_GetDeviceProcAddr(device, name);
}

/* ⚠ strdup(NULL). RADV calls it during initialisation and this console's libc
 * reads the argument with no check, so the process takes a fault at address 0
 * inside libSceLibcInternal and the driver never reports what it was missing:
 *
 *     syscall fault 0xc0000005 at libSceLibcInternal.sprx+0x4fa4d, address 0
 *       called from libvulkan.prx+0x1677e7f   -> strdup+0xf
 *
 * A NULL here means something RADV expected to be a string was not -- a device
 * name, a path, an environment value -- and on this console environ is its own
 * problem (docs/FINDINGS.md: setenv succeeds but never publishes it, and only
 * ntdll links pw_prx_env_ps4.o). Answer NULL as a sane libc would, and say so
 * once, so the next thing RADV reports is the missing value rather than a
 * fault with no name on it.
 *
 * strdup is already defined strongly in this link -- the driver archive comes
 * in whole -- so this goes through --wrap, the same way the cwd layer takes
 * over its 28 calls, rather than trying to replace the definition. */
char *__real_strdup(const char *text);

char *__wrap_strdup(const char *text)
{
    extern int sceKernelDebugOutText(int channel, const char *line);
    static int reported;

    if (!text) {
        if (reported < 4) {
            reported++;
            sceKernelDebugOutText(0, "libvulkan: strdup(NULL) refused\n");
        }
        return NULL;
    }
    return __real_strdup(text);
}

/* ⚠ A valid, empty environ. The SDK ships libc only as a static archive, so
 * every PRX carries a private environment, and this console's setenv reports
 * success while never publishing it: environ stays NULL however many variables
 * are set. ntdll answers that by linking pw_prx_env_ps4.o and pointing environ
 * at its own list; nothing did it for this module.
 *
 * Mesa walks it. driParseConfigFiles reads driconf from the environment, and
 * walked straight into the NULL:
 *
 *     syscall fault 0xc0000005 at libSceLibcInternal.sprx+0xbb8af, address 0
 *       called from libvulkan.prx+0x13fa674  -> driParseConfigFiles+0xb24
 *
 * which is the same shape the ntdll note records for get_initial_environment.
 * An empty list is the honest value -- there are no variables here to read --
 * and it terminates on the first entry instead of faulting. The init array runs
 * from module_start, so this is in place before any entry point is called. */
extern char **environ;
extern char *program_invocation_short_name;
extern char *program_invocation_name;
static char *pw_empty_environ[1] = { NULL };

__attribute__((constructor)) static void pw_publish_environ(void)
{
    extern int sceKernelDebugOutText(int channel, const char *line);

    if (!environ) {
        environ = pw_empty_environ;
        sceKernelDebugOutText(0, "libvulkan: environ was NULL, published an empty one\n");
    }

    /* ⚠ And the process name, which is the NULL driconf actually met. Mesa's
     * util_get_process_name() returns program_invocation_short_name, a glibc
     * convention: the symbol exists here as a weak object the static libc
     * carries, but nothing on this console ever sets it, so it is NULL. That
     * NULL reaches driParseConfigFiles as the application name and is strdup'd
     * on the way -- which is why the strdup guard fired and the fault stayed in
     * driconf after environ turned out to be fine:
     *
     *     libvulkan: strdup(NULL) refused
     *     syscall fault ... at libSceLibcInternal.sprx+0xbb8af, address 0
     *       called from libvulkan.prx+0x13fa734  -> driParseConfigFiles+0xbe4
     *
     * A constant is the honest answer: driconf uses the name only to match
     * per-application workarounds, and there are none for a title that reports
     * itself as this. Both symbols are writable data in the module. */
    if (!program_invocation_short_name) {
        program_invocation_short_name = (char *)"prospero-win";
        sceKernelDebugOutText(0, "libvulkan: process name was NULL, set to prospero-win\n");
    }
    if (!program_invocation_name) program_invocation_name = program_invocation_short_name;
}
