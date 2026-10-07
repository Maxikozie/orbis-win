/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* FreeBSD calls this console declares but does not implement.
 *
 * ⚠ THESE EXIST BECAUSE create-fself REFUSES AN UNRESOLVED IMPORT. Converting
 * ntdll.so to a PRX failed with
 *
 *     Failed to build FSELF: missing library for symbol (getmntinfo)
 *
 * — a .so may leave a symbol undefined, a PRX may not. Declaring the function
 * in a header was enough to compile and link an ELF, and is not enough to make
 * a loadable module.
 *
 * Each one here answers the way the absent subsystem would: no filesystems, no
 * data. Wine's own error paths then run. The alternative — leaving them
 * undefined — produces a module the console cannot load at all. */
#include <sys/types.h>
#include <errno.h>

struct statfs;

/* No mounted-filesystem enumeration on this console. Returning 0 means "zero
 * filesystems", which Wine reads as nothing to report rather than an error, and
 * is true: it has no /etc/fstab and no mount table to walk. */
int getmntinfo(struct statfs **mntbufp, int flags)
{
    (void)flags;
    if (mntbufp) *mntbufp = 0;
    return 0;
}
