/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_WINE_DL_PS4_H
#define PW_WINE_DL_PS4_H
/* The PS4's filling of prospero-win's module-loading vtable. The dlopen/dlsym/
 * dladdr logic and the PRXDESC1 parsing above it are upstream's, unchanged. */
#include "pw_wine_dl.h"
#include "pw_wine_prx.h"
#include "prospero_win.h"

/* report may be NULL, in which case failures go to stderr. */
int pw_wine_dl_ps4_ops(PwWineDlOps *ops, void (*report)(const char *));
#endif
