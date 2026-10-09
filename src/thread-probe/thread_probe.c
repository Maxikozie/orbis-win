/* How many threads can this console's pthread_create make, and what decides it?
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
    say("cap %d per trial; threads park until the trial ends, so this is how", MAX_THREADS);
    say("many can exist at once, not how many can be made in a row.");

    for (unsigned i = 0; i < sizeof(trials) / sizeof(trials[0]); i++) {
        char detail[160];
        int why = 0;
        unsigned made = run_trial(&trials[i], threads, stacks, &why);

        say("%s", trials[i].name);
        say("   created %u, then: %s", made, explain(why, detail, sizeof(detail)));
        finish(&trials[i], threads, stacks, made);
        say("   joined; next trial starts from nothing");
    }

    say("=== done ===");
    for (;;) sleep(1);
    return 0;
}
