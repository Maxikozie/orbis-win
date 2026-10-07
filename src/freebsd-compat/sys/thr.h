/* FreeBSD's <sys/thr.h>. The PS4 exports no thr_self — the SDK knows it only as
 * syscall 432 — but it DOES export pthread_getthreadid_np, which answers the
 * same question and is a plain function call.
 *
 * ⚠ THIS SETTLES AN OPEN QUESTION from the thread-registry work. prospero-win's
 * registry maps a kernel thread id onto a pthread_t so wineserver's thr_kill2
 * can find it, and which `long` to register here was unresolved: musl's
 * syscall() is on the link line but uses Linux conventions. pthread_getthreadid_np
 * removes the question — it is exported by libkernel, verified against the stub
 * library, and needs no syscall convention at all. */
#ifndef _SYS_THR_H_
#define _SYS_THR_H_
#include <sys/types.h>

int pthread_getthreadid_np(void);

/* thr_self's shape, for callers that want it. Not exported on this console;
 * declared so a conditional user compiles, and pthread_getthreadid_np is what
 * actually works. */
static inline int thr_self(long *id)
{
    if (!id) return -1;
    *id = (long)pthread_getthreadid_np();
    return 0;
}
#endif
