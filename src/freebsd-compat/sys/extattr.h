/* FreeBSD extended attributes. The SDK omits this header; Orbis file systems
 * carry no extattrs, so prospero-win's pw_wine_compat_libc.c defines every
 * entry point as a failing stub and needs only the declarations and the
 * namespace constants. Values are FreeBSD's. */
#ifndef PW_FBSD_SYS_EXTATTR_H
#define PW_FBSD_SYS_EXTATTR_H
#include <sys/types.h>

#define EXTATTR_NAMESPACE_EMPTY  0x00000000
#define EXTATTR_NAMESPACE_USER   0x00000001
#define EXTATTR_NAMESPACE_SYSTEM 0x00000002

#define EXTATTR_MAXNAMELEN 255

#ifdef __cplusplus
extern "C" {
#endif
ssize_t extattr_get_fd(int, int, const char *, void *, size_t);
ssize_t extattr_set_fd(int, int, const char *, const void *, size_t);
int     extattr_delete_fd(int, int, const char *);
ssize_t extattr_list_fd(int, int, void *, size_t);
ssize_t extattr_get_file(const char *, int, const char *, void *, size_t);
ssize_t extattr_set_file(const char *, int, const char *, const void *, size_t);
int     extattr_delete_file(const char *, int, const char *);
ssize_t extattr_list_file(const char *, int, void *, size_t);
ssize_t extattr_get_link(const char *, int, const char *, void *, size_t);
ssize_t extattr_set_link(const char *, int, const char *, const void *, size_t);
int     extattr_delete_link(const char *, int, const char *);
ssize_t extattr_list_link(const char *, int, void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
