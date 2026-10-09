/* How many threads can this console's pthread_create make, and what decides it?
 *
 * ── first run, 2026-10-09 ──────────────────────────────────────────────────
 *   A  wine's own: 1 MiB stack, system scope   created 146, then EAGAIN
 *   B  256 KiB stack, system scope             created 192 (the cap)
 *   C  library's stack, system scope           created  28, then EAGAIN
 *   D  1 MiB stack, process scope              created   0: mmap EINVAL
 *   E  wine's own again, after A..D joined     created   0: mmap EINVAL
 *
 * A is the answer to the original question: a bare title makes 146 threads
 * with exactly the attributes Wine asks for, and Wine dies at 13. Nothing is
 * wrong with pthread_create, the 1 MiB stack or system scope -- the resource
 * is spent before Wine ever starts making threads.
 *
 * ⚠ And D and E are the lead. mmap itself began refusing with EINVAL after
 * A..C, with every thread joined and every stack unmapped -- so whatever was
 * consumed did not come back. A+B+C map 338 regions; Wine's own counters at
 * the moment it fails read mmap=378 munmap=2 mprotect=1301. Both land in the
 * same few hundred. The suspect is the number of VM map entries a process may
 * hold, which mprotect splits and munmap may not return.
 *
 * So this run asks that directly, and separates count from size: F finds the
 * raw mapping ceiling, G asks whether unmapping gives it back, H splits one
 * mapping with mprotect and then counts threads. Flexible memory is reported
 * throughout, so a size limit would show as the number falling while the
 * count does not.
 *
 * With the request-pipe fix in (wine patch 0866) threads finally work on this
 * port, and the next wall is immediate: creation fails at about a dozen.
 *
 *     wine-ps5: spawn_thread tid=0068 FAILED pthread_create=-2147352541
 *               threads=13 stack=1027e70000+1048576
 *
 * -2147352541 is 0x80020023, which is how an SCE call behind a POSIX name
 * answers: 0x8002_0000 | errno, errno 0x23 = EAGAIN (src/dup-probe says the
 * same of its own results). EAGAIN is a resource ceiling, not a heap -- and
 * Wine turns every pthread_create failure into STATUS_NO_MEMORY, which sent
 * this the wrong way for a while.
 *
 * Which resource is the question, and Wine asks for a particular kind of
 * thread: PTHREAD_SCOPE_SYSTEM, a caller-supplied 1 MiB stack, no guard page
 * (dlls/ntdll/unix/thread.c, spawn_thread). Each of those is a candidate, so
 * each gets a trial, and every trial is the same measurement: create until
 * refused, say how many and why.
 *
 *   A  exactly what Wine asks for          -- the number to beat
 *   B  the same with a 256 KiB stack       -- is it the stack's size
 *   C  the library's own stack             -- is it setstack at all
 *   D  process scope instead of system     -- is it the kernel thread
 *   E  A again, after A's threads have     -- is the ceiling concurrent
 *      exited and been joined                 threads or total creations
 *
 * The threads park until released, so a trial measures how many can exist at
 * once rather than how fast they can be made. Output goes to klog.
 *
 * ⚠ Does not return from main(): returning is reported as CE-34878-0 on this
 * console and reads exactly like a crash (examples/triangle/triangle.c).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>

#include <orbis/libkernel.h>

enum { MAX_THREADS = 192, STACK_BIG = 1024 * 1024, STACK_SMALL = 256 * 1024 };

static void say(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
}

/* Set once to let every parked thread go. */
static volatile int release;

static void *park(void *arg)
{
    (void)arg;
    while (!__atomic_load_n(&release, __ATOMIC_ACQUIRE)) usleep(2000);
    return NULL;
}

struct trial {
    const char *name;
    size_t stack;       /* 0: let the library allocate one */
    int system_scope;
};

/* Create until refused. Returns how many were made; *why is the refusal. */
static unsigned run_trial(const struct trial *t, pthread_t *threads, void **stacks, int *why)
{
    unsigned made = 0;
    int rc = 0;

    __atomic_store_n(&release, 0, __ATOMIC_RELEASE);
    for (made = 0; made < MAX_THREADS; made++) {
        pthread_attr_t attr;
        void *stack = NULL;

        pthread_attr_init(&attr);
        if (t->system_scope) pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        if (t->stack) {
            /* Wine's stacks come from its own mapping, not from the library. */
            stack = mmap(NULL, t->stack, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (stack == MAP_FAILED) { rc = -1; pthread_attr_destroy(&attr); break; }
            pthread_attr_setstack(&attr, stack, t->stack);
            pthread_attr_setguardsize(&attr, 0);
        }
        rc = pthread_create(&threads[made], &attr, park, NULL);
        pthread_attr_destroy(&attr);
        if (rc) {
            if (stack) munmap(stack, t->stack);
            break;
        }
        stacks[made] = stack;
    }
    *why = rc;
    return made;
}

static void finish(const struct trial *t, pthread_t *threads, void **stacks, unsigned made)
{
    __atomic_store_n(&release, 1, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < made; i++) {
        pthread_join(threads[i], NULL);
        if (stacks[i]) munmap(stacks[i], t->stack);
    }
}

static const char *explain(int rc, char *out, size_t size)
{
    unsigned u = (unsigned)rc;

    if (!rc) { snprintf(out, size, "reached the cap, never refused"); return out; }
    if (rc == -1) { snprintf(out, size, "mmap for the stack failed, errno %d", errno); return out; }
    /* An SCE call behind a POSIX name answers 0x8002_0000 | errno. */
    if (u >= 0x80000000u) snprintf(out, size, "SCE 0x%08x -> errno %u (%s)", u, u & 0xFFFF,
                                   (u & 0xFFFF) == 35 ? "EAGAIN, a resource ceiling" :
                                   (u & 0xFFFF) == 12 ? "ENOMEM, a heap" :
                                   (u & 0xFFFF) == 22 ? "EINVAL, the arguments" : "?");
    else snprintf(out, size, "errno %d (%s)", rc,
                  rc == 35 ? "EAGAIN, a resource ceiling" :
                  rc == 12 ? "ENOMEM, a heap" :
                  rc == 22 ? "EINVAL, the arguments" : "?");
    return out;
}

/* ⚠ The SDK declares "int32_t sceKernelAvailableFlexibleMemorySize(size_t);"
 * -- by value, which is not the call. It takes a size_t * to fill in. Bind the
 * real symbol under a local name with the true signature, exactly as
 * src/dup-probe does for sceKernelRdup. */
extern int pw_available_flexible(size_t *out) __asm__("sceKernelAvailableFlexibleMemorySize");

/* Flexible memory still available, in KiB, or -1. Reported around every
 * trial: if the ceiling is a size this moves, and if it is a count it does
 * not. */
static long flexible_kib(void)
{
    size_t avail = 0;

    if (pw_available_flexible(&avail)) return -1;
    return (long)(avail >> 10);
}

enum { MAX_MAPS = 1024, MAP_UNIT = 64 * 1024 };
static void *maps[MAX_MAPS];

/* F/G: how many mappings can exist at once, and does unmapping give them
 * back? The answer decides whether Wine's problem is its 378 mappings or its
 * 1301 mprotects. */
static void trial_mappings(void)
{
    unsigned made = 0, freed = 0, again = 0;
    int err = 0;

    say("F  how many mappings at once (%u KiB each)", MAP_UNIT >> 10);
    say("   flexible before: %ld KiB", flexible_kib());
    for (made = 0; made < MAX_MAPS; made++) {
        maps[made] = mmap(NULL, MAP_UNIT, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (maps[made] == MAP_FAILED) { err = errno; break; }
    }
    say("   created %u%s", made, made == MAX_MAPS ? " (the cap)" : "");
    if (made < MAX_MAPS) say("   refused with errno %d", err);
    say("   flexible after: %ld KiB", flexible_kib());

    for (unsigned i = 0; i < made; i++)
        if (munmap(maps[i], MAP_UNIT) == 0) freed++;
    say("G  unmapped %u of %u; flexible now: %ld KiB", freed, made, flexible_kib());

    for (again = 0; again < made; again++) {
        void *p = mmap(NULL, MAP_UNIT, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) break;
        maps[again] = p;
    }
    say("   remapped %u of %u -- %s", again, made,
        again >= made ? "the ceiling is on mappings alive at once"
                      : "unmapping did NOT give it back, which is the Wine case");
    for (unsigned i = 0; i < again; i++) munmap(maps[i], MAP_UNIT);
}

/* H: split one mapping into many entries with mprotect, the way Wine's 1301
 * mprotects do, then count threads. If the thread ceiling falls, VM entries
 * are the resource Wine is spending. */
static void trial_mprotect_pressure(pthread_t *threads, void **stacks)
{
    static const struct trial wine_like = { "", STACK_BIG, 1 };
    const size_t span = 16 * 1024 * 1024;
    unsigned splits = 0;
    char *base;
    int why = 0;
    unsigned made;

    say("H  threads after mprotect has split one mapping into many entries");
    base = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { say("   mmap for the span failed, errno %d", errno); return; }
    /* Every other page read-only: each one carves the region in two. */
    for (size_t off = 0; off + 0x2000 <= span; off += 0x2000)
        if (mprotect(base + off, 0x1000, PROT_READ) == 0) splits++;
    say("   %u mprotect splits; flexible: %ld KiB", splits, flexible_kib());

    made = run_trial(&wine_like, threads, stacks, &why);
    {
        char detail[160];
        say("   created %u, then: %s", made, explain(why, detail, sizeof(detail)));
    }
    finish(&wine_like, threads, stacks, made);
    munmap(base, span);
    say("   (A made 146 with no splits -- a fall here names VM entries)");
}

int main(void)
{
    static pthread_t threads[MAX_THREADS];
    static void *stacks[MAX_THREADS];
    static const struct trial trials[] = {
        { "A  wine's own: 1 MiB stack, system scope", STACK_BIG,   1 },
        { "B  256 KiB stack, system scope",           STACK_SMALL, 1 },
        { "C  library's stack, system scope",         0,           1 },
        { "D  1 MiB stack, process scope",            STACK_BIG,   0 },
        { "E  wine's own again, after A..D joined",   STACK_BIG,   1 },
    };

    say("=== thread probe: how many threads, and what refuses them ===");
    say("flexible at start: %ld KiB", flexible_kib());
    say("cap %d threads per trial; threads park until the trial ends, so this", MAX_THREADS);
    say("is how many can exist at once, not how many can be made in a row.");
    /* ⚠ Order matters, and F/G is why it is first. The last run showed mmap
     * refusing after earlier trials had been joined and unmapped, so a trial
     * may be spending something its predecessors never gave back. F/G asks
     * that outright; read it before trusting any number below it. */
    trial_mappings();
    trial_mprotect_pressure(threads, stacks);

    for (unsigned i = 0; i < sizeof(trials) / sizeof(trials[0]); i++) {
        char detail[160];
        int why = 0;
        unsigned made = run_trial(&trials[i], threads, stacks, &why);

        say("%s", trials[i].name);
        say("   created %u, then: %s", made, explain(why, detail, sizeof(detail)));
        say("   flexible: %ld KiB", flexible_kib());
        finish(&trials[i], threads, stacks, made);
    }
    /* ⚠ A ran first last time and made 146. It runs last now, so if it still
     * makes 146 nothing above it was destructive and every number here stands
     * on its own; if it collapses, they do not, and only F/G and H mean
     * anything. */
    {
        static const struct trial again = { "A' wine's own, last instead of first", STACK_BIG, 1 };
        char detail[160];
        int why = 0;
        unsigned made = run_trial(&again, threads, stacks, &why);

        say("%s", again.name);
        say("   created %u, then: %s", made, explain(why, detail, sizeof(detail)));
        say("   146 here means the trials above cost nothing lasting;");
        say("   far fewer means every number after F is degraded.");
        finish(&again, threads, stacks, made);
    }

    say("=== done ===");
    for (;;) sleep(1);
    return 0;
}
