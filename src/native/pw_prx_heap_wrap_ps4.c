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

void *pw_wine_heap_malloc(size_t bytes);
void *pw_wine_heap_calloc(size_t count, size_t bytes);
void *pw_wine_heap_realloc(void *pointer, size_t bytes);
void pw_wine_heap_free(void *pointer);

void *__wrap_malloc(size_t bytes) { return pw_wine_heap_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t bytes) { return pw_wine_heap_calloc(count, bytes); }
void *__wrap_realloc(void *pointer, size_t bytes) { return pw_wine_heap_realloc(pointer, bytes); }
void __wrap_free(void *pointer) { pw_wine_heap_free(pointer); }
