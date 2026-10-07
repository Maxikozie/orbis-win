/* The SDK's sys/resource.h has no CPUSTATES. ntdll/unix/system.c reads the
 * per-CPU time buckets through it. #include_next so nothing is lost. */
#ifndef _FREEBSD_COMPAT_SYS_RESOURCE_H_
#define _FREEBSD_COMPAT_SYS_RESOURCE_H_
#include_next <sys/resource.h>

#ifndef CPUSTATES
#define CP_USER     0
#define CP_NICE     1
#define CP_SYS      2
#define CP_INTR     3
#define CP_IDLE     4
#define CPUSTATES   5
#endif
#endif
