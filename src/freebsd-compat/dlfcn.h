/* The SDK's dlfcn.h (musl shape) defines RTLD_NEXT and RTLD_DEFAULT but not
 * RTLD_SELF, which FreeBSD has and prospero-win's pw_wine_dl_libc.c uses.
 * include_next picks up the SDK header first, then adds only what is missing. */
#ifndef PW_FBSD_DLFCN_H
#define PW_FBSD_DLFCN_H
#include_next <dlfcn.h>
#ifndef RTLD_SELF
#define RTLD_SELF ((void *)-3)   /* FreeBSD rtld: search from the caller on */
#endif
#endif
