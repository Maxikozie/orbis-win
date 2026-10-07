/* FreeBSD 9 <sys/filio.h> — FIODTYPE, which ntdll/unix/file.c uses to ask what
 * kind of device a descriptor refers to. */
#ifndef _SYS_FILIO_H_
#define _SYS_FILIO_H_
#include <sys/ioccom.h>
#define FIOCLEX     _IO('f', 1)
#define FIONCLEX    _IO('f', 2)
#define FIONREAD    _IOR('f', 127, int)
#define FIONBIO     _IOW('f', 126, int)
#define FIOASYNC    _IOW('f', 125, int)
#define FIODTYPE    _IOR('f', 122, int)   /* get the device type */
#endif
