/* Fault-delivery and ucontext-layout probe.
 *
 * Answers the two questions blocking the Wine port, in one run:
 *
 *   1. Does a fault reach a sigaction-installed SIGSEGV handler on PS4?
 *      prospero-win's patches 0610, 0710 and 0883 all assume it does, and that
 *      holds on PS5. alechurri/nx-on-orbis says it does NOT on PS4 ("faults
 *      never reach sigaction there") and installs Sony's handler instead.
 *
 *   2. Is the mcontext at ucontext + 64, and does mc_len read 800 there?
 *      We ran on +16 (FreeBSD's) until nx-on-orbis put it at +64.
 *
 * ⚠ READS ONLY NAMED OFFSETS, NEVER A RANGE. The earlier signal probe scanned
 * 2048 bytes out of a ~800-byte context from inside a handler and took the
 * title down. Nothing here is trusted until mc_len reads 800.
 *
 * Each phase faults on its own PROT_NONE page and the handler mprotects it
 * writable, so the faulting instruction retries and succeeds -- no setjmp, and
 * the probe keeps running after a fault it successfully caught.
 *
 * The Sony phase runs FIRST: it is the one expected to work, so it proves the
 * harness before the open question is asked. If the sigaction phase is the one
 * that dies, every result is already in the log.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <orbis/libkernel.h>

/* Sony's exception handler. Not in the OpenOrbis headers; declared the way
 * nx-on-orbis declares it (frontend/ps4_platform.cpp). */
extern int sceKernelInstallExceptionHandler(int signal, void (*handler)(int, void *));
extern int sceKernelRemoveExceptionHandler(int signal);

#define PROBE_SIGSEGV 11
#define PAGE 0x4000u            /* Orbis pages are 16 KiB */

/* FreeBSD 9 amd64 mcontext, offsets within the mcontext itself. */
#define MC_TRAPNO 128
#define MC_ADDR   136
#define MC_RIP    160
#define MC_LEN    200

#define MC_AT_64  64            /* nx-on-orbis: 16-byte mask + 48 padding */
#define MC_AT_16  16            /* FreeBSD's own, what we assumed before */

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
}

static volatile int   sony_fired, posix_fired;
static volatile uint64_t len_at_64, len_at_16, seen_addr, seen_rip, seen_trapno;
static void *page_sony, *page_posix;

static uint64_t rd(const void *base, size_t off)
{
    uint64_t v;
    memcpy(&v, (const unsigned char *)base + off, sizeof(v));
    return v;
}

/* Record from inside the handler. mc_len is read at both candidate offsets and
 * is the gate: the registers are only read if it says 800, so a wrong offset
 * reports nonsense in one number instead of four plausible-looking ones. */
static void record(const void *uc)
{
    len_at_64 = rd(uc, MC_AT_64 + MC_LEN);
    len_at_16 = rd(uc, MC_AT_16 + MC_LEN);
    if (len_at_64 == 800) {
        seen_trapno = rd(uc, MC_AT_64 + MC_TRAPNO);
        seen_addr   = rd(uc, MC_AT_64 + MC_ADDR);
        seen_rip    = rd(uc, MC_AT_64 + MC_RIP);
    }
}

static void sony_handler(int sig, void *uc)
{
    (void)sig;
    sony_fired = 1;
    record(uc);
    mprotect(page_sony, PAGE, PROT_READ | PROT_WRITE);
}

static void posix_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)si;
    posix_fired = 1;
    record(uc);
    mprotect(page_posix, PAGE, PROT_READ | PROT_WRITE);
}

/* An RW page turned PROT_NONE, so mprotect is proven to work before a fault
 * depends on it. */
static void *guard_page(const char *what)
{
    void *p = mmap(NULL, PAGE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { say("  %s: mmap failed", what); return NULL; }
    *(volatile unsigned char *)p = 1;          /* writable now */
    if (mprotect(p, PAGE, PROT_NONE) != 0) { say("  %s: mprotect PROT_NONE failed", what); return NULL; }
    return p;
}

int main(void)
{
    say("=== fault probe ===");
    say("page size assumed: %u", PAGE);

    page_sony  = guard_page("sony page");
    page_posix = guard_page("posix page");
    if (!page_sony || !page_posix) { say("VERDICT: setup failed, nothing measured"); return 1; }

    /* ---- phase 1: Sony's exception handler ---- */
    say("");
    say("phase 1: sceKernelInstallExceptionHandler");
    int rc = sceKernelInstallExceptionHandler(PROBE_SIGSEGV, sony_handler);
    say("  install rc = 0x%x", rc);
    say("  faulting now (write to PROT_NONE page)...");
    *(volatile unsigned char *)page_sony = 0x5a;
    say("  returned from the faulting store");
    say("  sony handler fired : %s", sony_fired ? "YES" : "no");
    if (sony_fired) {
        say("  mc_len at +64      : %llu   %s", (unsigned long long)len_at_64,
            len_at_64 == 800 ? "<-- 800, offset 64 CONFIRMED" : "<-- NOT 800");
        say("  mc_len at +16      : %llu   (the offset we assumed before)",
            (unsigned long long)len_at_16);
        if (len_at_64 == 800) {
            say("  mc_trapno          : %llu", (unsigned long long)seen_trapno);
            say("  mc_addr            : 0x%llx  (page was 0x%llx)",
                (unsigned long long)seen_addr, (unsigned long long)(uintptr_t)page_sony);
            say("  mc_rip             : 0x%llx", (unsigned long long)seen_rip);
            say("  addr matches page  : %s",
                (seen_addr & ~(uint64_t)(PAGE - 1)) == (uint64_t)(uintptr_t)page_sony ? "YES" : "no");
        }
    }
    sceKernelRemoveExceptionHandler(PROBE_SIGSEGV);

    /* ---- phase 2: POSIX sigaction, the open question ---- */
    say("");
    say("phase 2: sigaction (what prospero-win's 0610/0710/0883 rely on)");
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posix_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    rc = sigaction(SIGSEGV, &sa, &old);
    say("  sigaction rc = %d", rc);
    say("  faulting now -- IF THE LOG STOPS HERE, sigaction NEVER RECEIVED IT");
    say("  (that is itself the answer: Wine's fault path must move to Sony's handler)");
    *(volatile unsigned char *)page_posix = 0x5a;
    say("  returned from the faulting store");
    say("  posix handler fired: %s", posix_fired ? "YES" : "no");

    say("");
    say("=== VERDICT ===");
    say("  sony handler  : %s", sony_fired  ? "receives faults" : "does NOT");
    say("  sigaction     : %s", posix_fired ? "receives faults" : "does NOT");
    say("  mcontext at 64: %s", len_at_64 == 800 ? "confirmed (mc_len 800)" : "NOT confirmed");
    if (posix_fired)
        say("  => prospero-win's sigaction fault path works as-is on PS4.");
    else
        say("  => patches 0610/0710/0883 must install Sony's handler instead.");
    say("=== done ===");
    return 0;
}
