/* The SDK's sys/time.h has no struct clockinfo, which ntdll/unix/system.c
 * reads through the KERN_CLOCKRATE sysctl. #include_next so nothing the SDK
 * does declare is lost. */
#ifndef _FREEBSD_COMPAT_SYS_TIME_H_
#define _FREEBSD_COMPAT_SYS_TIME_H_
#include_next <sys/time.h>

#ifndef _STRUCT_CLOCKINFO_DECLARED
#define _STRUCT_CLOCKINFO_DECLARED
struct clockinfo {
    int hz;         /* clock frequency */
    int tick;       /* micro-seconds per hz tick */
    int spare;
    int stathz;     /* statistics clock frequency */
    int profhz;     /* profiling clock frequency */
};
#endif
#endif
