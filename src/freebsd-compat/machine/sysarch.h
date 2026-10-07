/* FreeBSD 9 <machine/sysarch.h>, amd64.
 *
 * ⚠ THIS IS THE ONE signal_x86_64.c NEEDS, and it is not what I expected to
 * find there. I had predicted patches 0500/0501 would break that file on the
 * Linux-vs-FreeBSD ucontext_t mismatch; instead it breaks on a missing
 * declaration for sysarch(), which Wine uses to read and set the FS base. */
#ifndef _MACHINE_SYSARCH_H_
#define _MACHINE_SYSARCH_H_

#define I386_GET_FSBASE     7
#define I386_SET_FSBASE     8
#define I386_GET_GSBASE     9
#define I386_SET_GSBASE     10

#define AMD64_GET_FSBASE    128
#define AMD64_SET_FSBASE    129
#define AMD64_GET_GSBASE    130
#define AMD64_SET_GSBASE    131

int sysarch(int, void *);
#endif
