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
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>      /* snprintf, for the stack-size note below */

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
    size_t bytes = strlen(text) + 1;
    char *copy = malloc(bytes);
    if (copy) memcpy(copy, text, bytes);
    return copy;
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

/* ⚠ General-dynamic TLS, which this console answers with NULL.
 *
 * RADV imports __tls_get_addr; the one it links returns NULL, and ACO uses
 * thread_local, so a field is read off that NULL and the driver dies the
 * moment DXVK asks it to compile anything:
 *
 *     err:vulkan:vkCreateComputePipelines Exception 0xc0000005 in Unix call.
 *     syscall fault 0xc0000005 at libvulkan.so+0x12f0f29, address 1120
 *       -> aco_compile_shader + 0x2e9
 *
 * docs/FINDINGS.md measured the same thing before: general-dynamic TLS answers
 * NULL here, which is why wowprospero is built -femulated-tls with its own
 * __emutls_get_address. RADV arrives as a prebuilt archive and cannot be
 * recompiled that way without rebuilding Mesa, so give it a __tls_get_addr
 * that works instead.
 *
 * ⚠ A zeroed block is not an approximation here, it is exact. This module's
 * PT_TLS is
 *
 *     TLS  filesz 0x000000  memsz 0x001130    .tbss only, NOBITS
 *
 * -- no .tdata at all, so every thread-local it has starts as zero and there
 * is no initialiser image to copy. 0x1130 bytes also covers the 0x1120 the
 * fault was reading. The block is rounded up and bounds-checked, so an offset
 * past the end reports itself rather than becoming a silent corruption.
 *
 * Through --wrap: the definition it links now comes from an archive member
 * that is pulled in for its neighbours, so replacing it outright collides. */
#define PW_TLS_BLOCK_BYTES 0x2000u

struct pw_tls_index { unsigned long module, offset; };

static pthread_key_t pw_tls_key;
static pthread_once_t pw_tls_once = PTHREAD_ONCE_INIT;
static int pw_tls_ready;

static void pw_tls_free(void *block) { free(block); }

static void pw_tls_create_key(void)
{
    pw_tls_ready = pthread_key_create(&pw_tls_key, pw_tls_free) == 0;
}

void *__wrap___tls_get_addr(struct pw_tls_index *index)
{
    extern int sceKernelDebugOutText(int channel, const char *line);
    static int reported;
    unsigned char *block;

    pthread_once(&pw_tls_once, pw_tls_create_key);
    if (!pw_tls_ready || !index) return NULL;
    if (index->offset >= PW_TLS_BLOCK_BYTES) {
        if (reported < 4) {
            reported++;
            sceKernelDebugOutText(0, "libvulkan: TLS offset past the block\n");
        }
        return NULL;
    }
    if (!(block = pthread_getspecific(pw_tls_key))) {
        if (!(block = calloc(1, PW_TLS_BLOCK_BYTES))) return NULL;
        if (pthread_setspecific(pw_tls_key, block)) { free(block); return NULL; }
    }
    return block + index->offset;
}

/* ⚠ A thread created without a stack size costs 16 MiB of flexible memory.
 *
 * GTA IV cannot start a thread after about a dozen, and the reason is not
 * threads: flexible memory is gone. At the moment pthread_create is refused
 * there are 3072 KiB left of 449536, and the regions holding it are these --
 *
 *     flexible_holder rank=1 bytes=378880K count=24 prot=0x3 name=orbis
 *     flexible_region rank=2 at=7d7bb0000 bytes=16384K prot=0x3 name=orbis
 *     flexible_region rank=3 at=7d8bb0000 bytes=16384K prot=0x3 name=orbis
 *     ...
 *
 * twenty-four contiguous 16 MiB mappings, 370 MiB of the 461 MiB this process
 * has mapped. src/thread-probe had already measured what a 16 MiB region is:
 * its trial C, threads taking the library's own stack, managed 28 before
 * flexible memory hit zero -- about 16 MiB each. These are thread stacks.
 *
 * Mesa starts its worker threads through scePthreadCreate (u_thread_create),
 * and where the attribute is NULL this console gives each one a 16 MiB stack.
 * Wine's own threads escape it only because spawn_thread hands over a stack of
 * its own.
 *
 * So supply the size Mesa does not. 4 MiB: generous for what these threads do
 * -- ACO and NIR, which recurse over a shader's control flow and not much else
 * -- and a quarter of what Linux hands a default thread, where the same code
 * runs. Twenty-four of them cost 96 MiB instead of 384 MiB. If a compile
 * thread ever faults deep in a pass, this is the number to raise first.
 *
 * ⚠ Only threads that asked for nothing, or for more than this, are changed. A
 * caller that chose its own smaller stack keeps it. */
enum { PW_RADV_THREAD_STACK = 4u << 20 };

int scePthreadAttrInit(pthread_attr_t *attr);
int scePthreadAttrDestroy(pthread_attr_t *attr);
int scePthreadAttrSetstacksize(pthread_attr_t *attr, size_t size);
int scePthreadAttrGetstacksize(const pthread_attr_t *attr, size_t *size);
int scePthreadAttrGetdetachstate(const pthread_attr_t *attr, int *state);
int scePthreadAttrSetdetachstate(pthread_attr_t *attr, int state);
int sceKernelDebugOutText(int channel, const char *text);

int __real_scePthreadCreate(pthread_t *thread, const pthread_attr_t *attr,
                            void *(*entry)(void *), void *arg, const char *name);

int __wrap_scePthreadCreate(pthread_t *thread, const pthread_attr_t *attr,
                            void *(*entry)(void *), void *arg, const char *name)
{
    static unsigned adjusted;
    pthread_attr_t local;
    size_t size = 0;
    int detach = 0, rc;

    if (attr && !scePthreadAttrGetstacksize(attr, &size) && size && size <= PW_RADV_THREAD_STACK)
        return __real_scePthreadCreate(thread, attr, entry, arg, name);
    if (scePthreadAttrInit(&local))
        return __real_scePthreadCreate(thread, attr, entry, arg, name);
    if (attr && !scePthreadAttrGetdetachstate(attr, &detach))
        scePthreadAttrSetdetachstate(&local, detach);
    scePthreadAttrSetstacksize(&local, PW_RADV_THREAD_STACK);
    rc = __real_scePthreadCreate(thread, &local, entry, arg, name);
    scePthreadAttrDestroy(&local);

    /* The first few, so a run says whether this module owns those twenty-four
     * mappings or only some of them. ⚠ Whether this channel carries from a PRX
     * is not certain -- it does from wineserver and does not from wowprospero
     * -- but the flexible-memory figures in the title's log answer the same
     * question without it. */
    if (adjusted++ < 8) {
        char line[160];

        snprintf(line, sizeof(line),
                 "radv: thread %u \"%s\" asked %zuK, given %uK\n",
                 adjusted, name ? name : "-", size >> 10, PW_RADV_THREAD_STACK >> 10);
        sceKernelDebugOutText(0, line);
    }
    return rc;
}
