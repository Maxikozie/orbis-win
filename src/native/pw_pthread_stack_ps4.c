/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Preserve Wine's explicitly supplied kernel stacks in ntdll.
 *
 * The SDK pthread_create interposer raises small stacks to the creator's
 * stack size and builds fresh attributes without copying the stack address.
 * Wine's 1 MiB direct-memory stack thus becomes a new 16 MiB flexible-memory
 * stack. A dozen workers exhaust the pool. Wine installs its own signal
 * stack in ntdll/unix/server.c, so these workers do not need the SDK's
 * alternate-stack trampoline. Calls without a supplied stack keep it.
 */
#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>

extern int scePthreadCreate(pthread_t *, const pthread_attr_t *,
                           void *(*)(void *), void *, const char *);
extern int sceKernelDebugOutText(int, const char *);
extern int __real_pthread_create(pthread_t *, const pthread_attr_t *,
                                 void *(*)(void *), void *);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    void *stack = NULL;
    size_t bytes = 0;
    char line[240];
    pthread_attr_t actual;
    void *actual_stack = NULL;
    size_t actual_bytes = 0;
    int query = -1;
    int status;

    if (!attr || pthread_attr_getstack(attr, &stack, &bytes) || !stack)
        return __real_pthread_create(thread, attr, start, arg);

    status = scePthreadCreate(thread, attr, start, arg, "wine-ps4");
    if (!status && !pthread_attr_init(&actual)) {
        query = pthread_attr_get_np(*thread, &actual);
        if (!query) query = pthread_attr_getstack(&actual, &actual_stack, &actual_bytes);
        pthread_attr_destroy(&actual);
    }
    snprintf(line, sizeof(line), "wine-ps4: supplied stack=%p bytes=%zuK create=%d actual=%p+%zuK query=%d\n",
             stack, bytes >> 10, status, actual_stack, actual_bytes >> 10, query);
    sceKernelDebugOutText(0, line);
    return status;
}
