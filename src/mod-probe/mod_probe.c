/* Which kind of PRX will sceKernelLoadStartModule accept?
 *
 * The title finds ntdll.prx and the loader rejects it:
 *     load status=-2147352568 stage=1 module=0x80020008    (ENOEXEC)
 * straight out of sceKernelLoadStartModule. Our module is structurally
 * indistinguishable from Sony's libc.prx -- same FSELF header shape, same
 * inner ELF type 0xfe18 (ET_SCE_DYNAMIC) -- so the difference is in how
 * create-fself stamps it, and one reinstall per guess is too slow.
 *
 * This ships every candidate and tries them all in one run, with Sony's own
 * libc.prx as the control: if that fails too, the problem is the call or the
 * path, not our modules.
 *
 * Output goes to klog (sceKernelDebugOutText), which needs no library, no
 * configuration and no network.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <orbis/libkernel.h>

/* Both are declared by <orbis/libkernel.h>. Note the load returns uint32_t:
 * an error such as 0x80020008 is a large POSITIVE value, so "> 0" is not a
 * success test -- the error bit is. */

static void say(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
}

static void try_load(const char *what, const char *path)
{
    int res = 0;
    uint32_t h = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &res);
    if (!(h & 0x80000000u)) {
        say("  %-22s LOADED  handle=0x%x res=0x%x", what, h, res);
        sceKernelStopUnloadModule((OrbisKernelModule)h, 0, NULL, 0, NULL, NULL);
    } else {
        /* 0x80020002 ENOENT, 0x80020008 ENOEXEC, 0x8002000d EACCES */
        const char *name = "?";
        unsigned e = h;
        if (e == 0x80020002u) name = "ENOENT";
        else if (e == 0x80020008u) name = "ENOEXEC";
        else if (e == 0x8002000du) name = "EACCES";
        else if (e == 0x80020016u) name = "EINVAL";
        say("  %-22s failed  0x%x (%s)", what, e, name);
    }
}

int main(void)
{
    say("=== module probe ===");

    say("control: a Sony module the console certainly loads");
    try_load("libc.prx", "/app0/sce_module/libc.prx");

    say("our ntdll, by program type:");
    try_load("ptype fake",          "/app0/variants/ntdll_fake.prx");
    try_load("ptype npdrm_dynlib",  "/app0/variants/ntdll_npdrm_dynlib.prx");
    try_load("ptype system_dynlib", "/app0/variants/ntdll_system_dynlib.prx");
    try_load("ptype npdrm_exec",    "/app0/variants/ntdll_npdrm_exec.prx");

    say("same file, different directory (is the path the gate?):");
    try_load("in sce_module",       "/app0/sce_module/ntdll_fake.prx");
    try_load("in win/.../x86_64-unix", "/app0/win/wine/lib/wine/x86_64-unix/ntdll.prx");

    say("=== done ===");
    return 0;
}
