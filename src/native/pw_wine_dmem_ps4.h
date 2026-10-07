/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_WINE_DMEM_PS4_H
#define PW_WINE_DMEM_PS4_H
/* The PS4's filling of prospero-win's direct-memory vtable. The region policy
 * above it is upstream's pw_wine_dmem.c, unchanged. */
#include "pw_wine_dmem.h"
#include "prospero_win.h"

int pw_wine_dmem_ps4_ops(PwWineDmemOps *);
#endif
