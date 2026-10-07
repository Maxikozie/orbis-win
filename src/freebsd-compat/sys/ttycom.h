/* FreeBSD 9 <sys/ttycom.h> — the window-size ioctl server.c and env.c want. */
#ifndef _SYS_TTYCOM_H_
#define _SYS_TTYCOM_H_
#include <sys/ioccom.h>
/* ⚠ GUARDED: something else in this SDK already declares winsize, and an
 * unguarded copy here made configure's sys/mount.h test fail with "redefinition
 * of 'winsize'" — a failure attributed to the wrong header entirely. */
#ifndef _STRUCT_WINSIZE_DECLARED
#define _STRUCT_WINSIZE_DECLARED
struct winsize {
    unsigned short ws_row, ws_col;      /* characters */
    unsigned short ws_xpixel, ws_ypixel;/* pixels */
};
#endif
#define TIOCGWINSZ  _IOR('t', 104, struct winsize)
#define TIOCSWINSZ  _IOW('t', 103, struct winsize)
#endif
