/* FreeBSD's <sys/ioctl.h> pulls in the three command headers; this SDK's does
 * not, which is why TIOCGWINSZ, SIOCATMARK and FIODTYPE stayed undeclared even
 * after those headers existed — Wine includes <sys/ioctl.h> and expects them
 * through it. #include_next first so orbis-compat's own ioctl.h still applies. */
#ifndef _FREEBSD_COMPAT_SYS_IOCTL_H_
#define _FREEBSD_COMPAT_SYS_IOCTL_H_
#include_next <sys/ioctl.h>
#include <sys/ttycom.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#endif
