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

/* The i386 LDT operations. signal_x86_64.c's FreeBSD arm calls sysarch
 * (I386_SET_LDT, &args) to install a WoW64 segment.
 *
 * ⚠ AND PATCH 0500 SAYS THIS CONSOLE REFUSES IT — "WoW64 threads need no LDT
 * entry (the console refuses i386_set_ldt)". Declared so the FreeBSD arm
 * compiles; the call is expected to fail at runtime and Wine's own error path
 * handles that. Not defining it would block the build over a call that is
 * already known not to work. */
struct user_segment_descriptor;
struct i386_ldt_args {
    unsigned int start;
    struct user_segment_descriptor *descs;
    unsigned int num;
};
#define I386_GET_LDT  0
#define I386_SET_LDT  1

int sysarch(int, void *);
#endif
