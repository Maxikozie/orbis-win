/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Let the title write into THIS module's environment.
 *
 * The SDK ships libc only as a static archive, so the title and every PRX each
 * carry a private __environ. The title sets WINE_PS5_NTDLL_DIR with its own
 * setenv and Wine, running inside ntdll.prx, reads a different and empty one:
 *
 *     WINE wine: cannot get path to ntdll.so (set WINE_PS5_NTDLL_DIR)
 *
 * even though the title reported environment status=0.
 *
 * Linking the shared libc instead would unify them, but it also moves malloc,
 * exit and the whole __cxa_* family onto libc.prx -- 88 symbols -- and the
 * title then failed with PRX_NOT_RESOLVED_FUNCTION. This is the narrow version:
 * one exported function, writing into this module's environ and nothing else.
 *
 * Exported through the module's PRXDESC1 descriptor (scripts/prx-ps4.sh), and
 * also the reason setenv is pulled into the module at all -- nothing else in
 * ntdll calls it, so it would otherwise be absent from the static libc copy.
 *
 * ⚠ DIAGNOSTIC. Wine reads `environ` directly (get_initial_environment), and it
 * faulted at NULL there with the title reporting environment status=0 -- which
 * only covers the title's own setenv, never this one. Report what setenv did to
 * this module's environ, so "never called" and "called and did not take" stop
 * looking alike. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern char **environ;
int sceKernelDebugOutText(int channel, const char *text);

int pw_ps4_setenv(const char *name, const char *value);

int pw_ps4_setenv(const char *name, const char *value)
{
    int status;
    char line[200];
    size_t count = 0;

    if (!name || !*name || !value) return -1;
    status = setenv(name, value, 1);
    if (environ)
        while (environ[count]) count++;
    snprintf(line, sizeof(line), "[ps4-gladius] module setenv %s status=%d environ=%p entries=%zu\n",
             name, status, (void *)environ, count);
    sceKernelDebugOutText(0, line);
    return status;
}
