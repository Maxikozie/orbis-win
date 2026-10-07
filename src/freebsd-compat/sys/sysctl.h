/* orbis-compat already ships a sys/sysctl.h, and this adds to it rather than
 * replacing it: #include_next chains to that copy, which chains onward.
 *
 * ⚠ AND THE VALUES BELOW WILL NOT BE ANSWERED. orbis-compat's header says it
 * plainly — "this kernel has no sysctl; the declarations exist so those
 * compile, and everything except hw.ncpu still returns -1 so the caller takes
 * its own documented fallback". Wine's ntdll/unix/system.c asks KERN_CLOCKRATE
 * for the tick rate; it will get -1 here and use its fallback. Declaring the
 * name is what lets it reach that fallback instead of failing to build. */
#ifndef _FREEBSD_COMPAT_SYS_SYSCTL_H_
#define _FREEBSD_COMPAT_SYS_SYSCTL_H_
#include_next <sys/sysctl.h>

#ifndef KERN_CLOCKRATE
#define KERN_CLOCKRATE  12      /* struct clockinfo */
#endif
#ifndef KERN_BOOTTIME
#define KERN_BOOTTIME   21      /* struct timeval */
#endif
#ifndef KERN_ARND
#define KERN_ARND       37      /* random bytes */
#endif
#endif
