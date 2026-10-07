/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* thr_kill2 for the PS4, over prospero-win's thread registry.
 *
 * pw_wine_threads.c itself needs no porting — 62 lines of pthread, errno,
 * sched, signal and unistd, no Sony calls. This is its companion, which the
 * PS5 build keeps in pw_wine_threads_libc.c and which cannot be reused
 * verbatim: that file includes <sys/thr.h>, and **the PS4 SDK has no such
 * header**.
 *
 * ⚠ AND thr_self IS NOT AN EXPORTED FUNCTION HERE. The SDK knows it only as
 * syscall number 432 (`SYS_thr_self` in bits/syscall.h); libkernel exports no
 * thr_self, thr_kill or thr_kill2. That is precisely why this registry exists:
 * the in-process wineserver signals Wine's threads with thr_kill2(pid, lwpid,
 * sig), the console provides no such call, so ntdll registers each thread's
 * kernel id against its pthread_t and thr_kill2 becomes a lookup plus
 * pthread_kill.
 *
 * ⚠ OPEN QUESTION, DELIBERATELY NOT GUESSED AT. Which `long` ntdll will
 * register as the id on the PS4 is unsettled: musl's syscall() is on the link
 * line but uses Linux conventions, and whether SYS_thr_self works through it
 * on a FreeBSD-derived kernel has not been tested. It does not matter yet —
 * the registry is a map and any unique key works — but it must agree between
 * ntdll and wineserver when both are real.
 */
#include "pw_wine_threads.h"
#include <sys/types.h>

int thr_kill2(pid_t pid, long id, int sig)
{
    return pw_wine_thread_kill(pid, id, sig);
}
