/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_PAD_PS4_H
#define PW_PAD_PS4_H
/* The PS4's filling of prospero-win's pad vtable. Everything else about the
 * pad — batching, edges, generations, analog centring — is upstream's
 * native/pw_pad_ps5.c, compiled unmodified. */
#include "pw_pad_ps5.h"
#include "prospero_win.h"

/* Must not exceed PW_PAD_PS5_BATCH: the core hands down a buffer of that size
 * and this reads into a stack copy of its own. */
enum { PW_PAD_PS4_BATCH = PW_PAD_PS5_BATCH };

int pw_pad_ps4_platform_ops(PwPadPs5Ops *);
#endif
