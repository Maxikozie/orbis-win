/* FreeBSD 9 <sys/sockio.h> — the socket ioctl dlls/ntdll/unix/socket.c wants. */
#ifndef _SYS_SOCKIO_H_
#define _SYS_SOCKIO_H_
#include <sys/ioccom.h>
#define SIOCATMARK  _IOR('s', 7, int)   /* at out-of-band mark? */
#endif
