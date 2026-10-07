/* The SDK ships a sys/mount.h without the mount flags or getmntinfo.
 *
 * ⚠ IT DOES, HOWEVER, ALREADY HAVE struct statfs AND fsid_t — in bits/statfs.h
 * and sys/statfs.h respectively, the latter as `struct __fsid_t`. An earlier
 * version of this header declared both itself and produced
 *
 *   error: redefinition of 'statfs'
 *   error: typedef redefinition with different types
 *          ('struct fsid' vs 'struct __fsid_t')
 *
 * which is worse than it looks: the two definitions were not even the same
 * shape, so whichever won would have been silently wrong for the other's
 * callers. Including the SDK's own is the only correct answer.
 *
 * #include_next, not a replacement, so nothing the SDK's mount.h does declare
 * is lost. */
#ifndef _FREEBSD_COMPAT_SYS_MOUNT_H_
#define _FREEBSD_COMPAT_SYS_MOUNT_H_
#include_next <sys/mount.h>
#include <sys/statfs.h>

#ifndef MNT_RDONLY
#define MNT_RDONLY      0x00000001  /* read only filesystem */
#define MNT_SYNCHRONOUS 0x00000002
#define MNT_NOEXEC      0x00000004
#define MNT_NOSUID      0x00000008
#define MNT_LOCAL       0x00001000  /* filesystem is stored locally */
#define MNT_QUOTA       0x00002000
#define MNT_ROOTFS      0x00004000
#endif

/* The *statfs() family's `flags` argument. */
#ifndef MNT_WAIT
#define MNT_WAIT        1
#define MNT_NOWAIT      2
#define MNT_LAZY        3
#define MNT_SUSPEND     4
#endif

struct statfs;
int getmntinfo(struct statfs **, int);
#endif
