/* FreeBSD 9 <sys/event.h> — kqueue, which Wine's server uses for its poll loop.
 *
 * ⚠ AND THIS ONE IS REAL, not a declaration over a missing call: libkernel
 * exports both kqueue and kevent. Verified against the stub library rather than
 * assumed, because declaring a syscall the console does not have would turn a
 * compile error into a link error and then a runtime one. */
#ifndef _SYS_EVENT_H_
#define _SYS_EVENT_H_
#include <sys/types.h>
#include <stdint.h>

#define EVFILT_READ     (-1)
#define EVFILT_WRITE    (-2)
#define EVFILT_AIO      (-3)
#define EVFILT_VNODE    (-4)
#define EVFILT_PROC     (-5)
#define EVFILT_SIGNAL   (-6)
#define EVFILT_TIMER    (-7)
#define EVFILT_USER     (-11)

struct kevent {
    uintptr_t   ident;
    short       filter;
    unsigned short flags;
    unsigned int   fflags;
    intptr_t    data;
    void       *udata;
};

#define EV_SET(kevp, a, b, c, d, e, f) do {     \
    struct kevent *__kevp = (kevp);             \
    __kevp->ident  = (a); __kevp->filter = (b); \
    __kevp->flags  = (c); __kevp->fflags = (d); \
    __kevp->data   = (e); __kevp->udata  = (f); \
} while (0)

#define EV_ADD      0x0001
#define EV_DELETE   0x0002
#define EV_ENABLE   0x0004
#define EV_DISABLE  0x0008
#define EV_ONESHOT  0x0010
#define EV_CLEAR    0x0020
#define EV_RECEIPT  0x0040
#define EV_DISPATCH 0x0080
#define EV_EOF      0x8000
#define EV_ERROR    0x4000

#define NOTE_DELETE 0x00000001
#define NOTE_WRITE  0x00000002
#define NOTE_EXTEND 0x00000004
#define NOTE_ATTRIB 0x00000008
#define NOTE_LINK   0x00000010
#define NOTE_RENAME 0x00000020
#define NOTE_REVOKE 0x00000040
#define NOTE_TRIGGER 0x01000000  /* EVFILT_USER: fire the event */

struct timespec;
int kqueue(void);
int kevent(int, const struct kevent *, int, struct kevent *, int,
           const struct timespec *);
#endif
