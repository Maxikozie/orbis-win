/* FreeBSD 9 <sys/ttycom.h> — the window-size ioctls server.c and env.c want.
 *
 * ⚠ NO `struct winsize` HERE. The SDK's own sys/ioctl.h (and pty.h) declare it
 * with no guard macro, so a second copy is a hard redefinition — which cost 59
 * errors AND failed configure's unrelated sys/mount.h test, because that test
 * includes sys/ioctl.h too and the error was reported against ttycom.h. A
 * private _STRUCT_WINSIZE_DECLARED guard does not help: nothing else sets it.
 *
 * sys/ioctl.h in this directory chains to the SDK's copy before including this
 * one, so winsize is already declared by the time these macros are used. */
#ifndef _SYS_TTYCOM_H_
#define _SYS_TTYCOM_H_
#include <sys/ioccom.h>
struct winsize;
#define TIOCGWINSZ  _IOR('t', 104, struct winsize)
#define TIOCSWINSZ  _IOW('t', 103, struct winsize)
#endif
