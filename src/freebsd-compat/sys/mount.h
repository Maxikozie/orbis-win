/* The SDK ships a sys/mount.h that omits the mount flags and struct statfs.
 *
 * ⚠ #include_next, NOT A REPLACEMENT. Shadowing the SDK's copy would drop
 * whatever it does declare; this adds only what Wine asks for and is missing.
 * The same mechanism orbis-compat uses for its own corrections.
 *
 * struct statfs is also why __wrap_statfs in pw_wine_cwd_libc.c compiled with
 * "declaration of 'struct statfs' will not be visible outside of this function"
 * — it only forwards the pointer, so the incomplete type was harmless there. */
#ifndef _FREEBSD_COMPAT_SYS_MOUNT_H_
#define _FREEBSD_COMPAT_SYS_MOUNT_H_
#include_next <sys/mount.h>
#include <sys/param.h>
#include <sys/types.h>

#ifndef MNT_RDONLY
#define MNT_RDONLY      0x00000001  /* read only filesystem */
#define MNT_SYNCHRONOUS 0x00000002
#define MNT_NOEXEC      0x00000004
#define MNT_NOSUID      0x00000008
#define MNT_LOCAL       0x00001000  /* filesystem is stored locally */
#define MNT_QUOTA       0x00002000
#define MNT_ROOTFS      0x00004000
#endif

/* Flags for the *statfs() family's `flags` argument. */
#ifndef MNT_WAIT
#define MNT_WAIT        1
#define MNT_NOWAIT      2
#define MNT_LAZY        3
#define MNT_SUSPEND     4
#endif

/* ⚠ fsid_t IS NOT REACHABLE FROM HERE. The SDK declares `struct __fsid_t`
 * but does not expose the typedef through any header sys/mount.h may include,
 * and __BEGIN_DECLS does not exist in this SDK at all — both were real compile
 * errors in configure's test, which is why HAVE_SYS_MOUNT_H came back "no"
 * while the header sat right there. */
#ifndef _FSID_T_DECLARED
#define _FSID_T_DECLARED
typedef struct fsid { int32_t val[2]; } fsid_t;
#endif

#ifndef MFSNAMELEN
#define MFSNAMELEN  16
#define MNAMELEN    88
#endif

#ifndef _STRUCT_STATFS_DECLARED
#define _STRUCT_STATFS_DECLARED
struct statfs {
    uint32_t f_version;
    uint32_t f_type;
    uint64_t f_flags;
    uint64_t f_bsize;
    uint64_t f_iosize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    int64_t  f_bavail;
    uint64_t f_files;
    int64_t  f_ffree;
    uint64_t f_syncwrites;
    uint64_t f_asyncwrites;
    uint64_t f_syncreads;
    uint64_t f_asyncreads;
    uint64_t f_spare[10];
    uint32_t f_namemax;
    uid_t    f_owner;
    fsid_t   f_fsid;
    char     f_charspare[80];
    char     f_fstypename[MFSNAMELEN];
    char     f_mntfromname[MNAMELEN];
    char     f_mntonname[MNAMELEN];
};
#endif

int statfs(const char *, struct statfs *);
int fstatfs(int, struct statfs *);
int getfsstat(struct statfs *, long, int);
int getmntinfo(struct statfs **, int);
#endif
