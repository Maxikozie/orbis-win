/* FreeBSD 9's amd64 mcontext_t, under a name of its own.
 *
 * ⚠ WHY NOT JUST FIX mcontext_t. The SDK typedefs it inside
 *     #if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
 * in bits/signal.h, and our compile line passes -D_BSD_SOURCE=1, so we get
 * musl's LINUX mcontext_t: { gregset_t gregs; fpregset_t fpregs; ... } with no
 * mc_rsp, mc_rip or any of the register file Wine's FreeBSD arm reads. Dropping
 * _BSD_SOURCE does not help — the #else branch is an opaque
 * `unsigned long __space[32]`, which is 256 bytes where FreeBSD's is ~800.
 * There is no guard macro to pre-empt the typedef with, and shadowing
 * bits/signal.h wholesale would silently drop everything else it declares.
 *
 * So the struct is named separately and patch 0900 points UCONTEXT_MC at it.
 * That is a one-line change to a macro patch 0500 already defines as a cast,
 * and it leaves every `.mc_rsp` access in signal_x86_64.c untouched.
 *
 * Layout from FreeBSD 9 sys/amd64/include/ucontext.h, field for field. */
#ifndef _FREEBSD_COMPAT_MACHINE_FBSD_MCONTEXT_H_
#define _FREEBSD_COMPAT_MACHINE_FBSD_MCONTEXT_H_
#include <stdint.h>

struct pw_fbsd_mcontext {
    int64_t  mc_onstack;
    int64_t  mc_rdi, mc_rsi, mc_rdx, mc_rcx, mc_r8, mc_r9, mc_rax, mc_rbx;
    int64_t  mc_rbp, mc_r10, mc_r11, mc_r12, mc_r13, mc_r14, mc_r15;
    uint32_t mc_trapno;
    uint16_t mc_fs, mc_gs;
    int64_t  mc_addr;
    uint32_t mc_flags;
    uint16_t mc_es, mc_ds;
    int64_t  mc_err, mc_rip, mc_cs, mc_rflags, mc_rsp, mc_ss;
    int64_t  mc_len;              /* sizeof(mcontext_t) */
    int64_t  mc_fpformat;
    int64_t  mc_ownedfp;
    int64_t  mc_spare1[1];
    int64_t  mc_fpstate[64] __attribute__((aligned(16)));
    int64_t  mc_fsbase, mc_gsbase;
    int64_t  mc_spare2[6];
};

/* ⚠ THE OFFSET IS THE ONE THING STILL UNMEASURED ON THIS CONSOLE.
 *
 * FreeBSD's ucontext_t begins { sigset_t uc_sigmask; mcontext_t uc_mcontext; }
 * and its sigset_t is 16 bytes, so mcontext sits at +16. The PS5 was MEASURED
 * at +64 (patch 0500's own comment), which means Sony moved it there at least
 * once — so +16 is FreeBSD's answer, not necessarily this console's.
 *
 * PW_PS4_UCONTEXT_MC_OFFSET overrides it without a rebuild of anything but this
 * file, and mc_len carries sizeof(mcontext_t) which is a cheap self-check: read
 * it at the chosen offset and it should be 800. A wrong offset gives nonsense
 * there rather than silently wrong registers. */
#ifndef PW_PS4_UCONTEXT_MC_OFFSET
#define PW_PS4_UCONTEXT_MC_OFFSET 16
#endif
#endif
