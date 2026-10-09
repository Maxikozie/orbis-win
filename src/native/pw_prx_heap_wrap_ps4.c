/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Send one module's malloc family to ntdll's heap, by --wrap.
 *
 * wine/ps5/pw_wine_heap_libc.c does this by defining the libc names outright,
 * and that is what scripts/prx-ps4.sh links into each unixlib. A module whose
 * own link already defines them cannot take that file -- libfreetype.prx pulls
 * strdup, aligned_alloc and posix_memalign from the static libc and collides on
 * all three -- so wrap the four that matter instead.
 *
 * ⚠ Why it matters at all. Three separate bugs today came of a module binding
 * its own allocator: wineserver ran out of the title's 13 MiB libc arena,
 * win32u freed a block ntdll had allocated and faulted inside
 * libSceLibcInternal, and every unixlib took dlopen from a weak stub rather
 * than ntdll's. FreeType allocates the buffers win32u's font code frees, so the
 * two have to agree on whose heap it is. */
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void *pw_wine_heap_malloc(size_t bytes);
void *pw_wine_heap_calloc(size_t count, size_t bytes);
void *pw_wine_heap_realloc(void *pointer, size_t bytes);
void pw_wine_heap_free(void *pointer);

void *__wrap_malloc(size_t bytes) { return pw_wine_heap_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t bytes) { return pw_wine_heap_calloc(count, bytes); }
void *__wrap_realloc(void *pointer, size_t bytes) { return pw_wine_heap_realloc(pointer, bytes); }
void __wrap_free(void *pointer) { pw_wine_heap_free(pointer); }

/* RADV's aligned allocations must be released through the same heap. */
void *pw_wine_heap_memalign(size_t alignment, size_t bytes);
void *__wrap_memalign(size_t alignment, size_t bytes)
{
    return pw_wine_heap_memalign(alignment, bytes);
}
void *__wrap_aligned_alloc(size_t alignment, size_t bytes)
{
    return pw_wine_heap_memalign(alignment, bytes);
}
int __wrap_posix_memalign(void **out, size_t alignment, size_t bytes)
{
    if (!alignment || (alignment & (alignment - 1)) || alignment % sizeof(void *))
        return EINVAL;
    void *pointer = pw_wine_heap_memalign(alignment, bytes);
    if (!pointer) return ENOMEM;
    *out = pointer;
    return 0;
}

/* libc string helpers allocate too; their outputs use our wrapped free. */
char *__wrap_strndup(const char *text, size_t limit)
{
    size_t bytes = strnlen(text, limit);
    char *copy = pw_wine_heap_malloc(bytes + 1);
    if (copy) { memcpy(copy, text, bytes); copy[bytes] = 0; }
    return copy;
}
int __wrap_vasprintf(char **out, const char *format, va_list args)
{
    va_list measure;
    va_copy(measure, args);
    int length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (length < 0) return -1;
    char *text = pw_wine_heap_malloc((size_t)length + 1);
    if (!text) return -1;
    vsnprintf(text, (size_t)length + 1, format, args);
    *out = text;
    return length;
}
int __wrap_asprintf(char **out, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = __wrap_vasprintf(out, format, args);
    va_end(args);
    return length;
}

/* orbis-compat's C++ new calls our wrapped malloc. Its matching deletes
 * otherwise come from shared libc and must use the same Wine heap. */
void __wrap__ZdlPv(void *pointer) { pw_wine_heap_free(pointer); }
void __wrap__ZdaPv(void *pointer) { pw_wine_heap_free(pointer); }
void __wrap__ZdlPvm(void *pointer, size_t bytes)
{
    (void)bytes;
    pw_wine_heap_free(pointer);
}
