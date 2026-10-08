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
 * ntdll calls it, so it would otherwise be absent from the static libc copy. */
#include <stdlib.h>

int pw_ps4_setenv(const char *name, const char *value);

int pw_ps4_setenv(const char *name, const char *value)
{
    if (!name || !*name || !value) return -1;
    return setenv(name, value, 1);
}
