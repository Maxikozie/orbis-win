/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Emulated thread-local storage for the WoW64 CPU backend.
 *
 * ⚠ A `static __thread` in a shared object compiles to the general-dynamic TLS
 * model, which calls __tls_get_addr. The SDK's static libc supplies one and it
 * does not work here: wowprospero's `self` read back as NULL and the DBT
 * faulted on it the moment it began translating guest code --
 *
 *     err:seh:handle_syscall_fault syscall fault 0xc0000005
 *         at .../wowprospero.so+0x219bc, address 0        (get_thread)
 *     err:wow:BTCpuSimulate host exception 0xc0000005 in translated code
 *
 * -- about sixty thousand times a second, each one delivered, returned from and
 * retried, so the title span instead of crashing.
 *
 * ntdll and the other modules avoid this by never needing general-dynamic TLS;
 * this one needs it, for exactly one variable. Upstream's equivalent module
 * (tools/build_wow64native.sh) links emutls.o out of the PS5 SDK's libc, which
 * has no counterpart here, so -femulated-tls gets this implementation instead.
 * It is compiler-rt's contract: one key per control block, the block's initial
 * value copied into each thread's copy on first use, freed when the thread
 * ends. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

typedef struct __emutls_control
{
    size_t size;                              /* bytes in the object */
    size_t align;                             /* its alignment */
    union { uintptr_t index; void *address; } object;
    void *value;                              /* initial value, or NULL for zero */
} __emutls_control;

void *__emutls_get_address( __emutls_control *control );

/* index holds the pthread key plus one, so that zero still means "unallocated" */
static pthread_mutex_t emutls_lock = PTHREAD_MUTEX_INITIALIZER;

void *__emutls_get_address( __emutls_control *control )
{
    uintptr_t index;
    pthread_key_t key;
    void *object;
    size_t align;

    if (!control) return NULL;

    if (!(index = __atomic_load_n( &control->object.index, __ATOMIC_ACQUIRE )))
    {
        pthread_mutex_lock( &emutls_lock );
        if (!(index = control->object.index))
        {
            pthread_key_t created;
            if (pthread_key_create( &created, free ))
            {
                pthread_mutex_unlock( &emutls_lock );
                return NULL;
            }
            index = (uintptr_t)created + 1;
            __atomic_store_n( &control->object.index, index, __ATOMIC_RELEASE );
        }
        pthread_mutex_unlock( &emutls_lock );
    }
    key = (pthread_key_t)(index - 1);

    if ((object = pthread_getspecific( key ))) return object;

    align = control->align < sizeof(void *) ? sizeof(void *) : control->align;
    if (posix_memalign( &object, align, control->size ) || !object) return NULL;
    if (control->value) memcpy( object, control->value, control->size );
    else memset( object, 0, control->size );
    if (pthread_setspecific( key, object ))
    {
        free( object );
        return NULL;
    }
    return object;
}
