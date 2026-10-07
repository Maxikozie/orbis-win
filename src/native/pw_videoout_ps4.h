/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* prospero-win's presenter, for the PlayStation 4.
 *
 * The PS5 original (native/pw_videoout_ps5.c) draws a TILED frame into a third
 * buffer and has AGC DMA it into whichever scanout buffer is not on screen,
 * then flips with sceAgcDcbSetFlip — ~350 lines across pw_agc_ps5.c and
 * pw_videoout_tile.h.
 *
 * None of that is needed here, for two reasons the PS4 gives us for free:
 *
 *   ORBIS_VIDEO_OUT_TILING_MODE_LINEAR   scanout reads a linear buffer, so the
 *                                        tiling pass and its index tables go
 *   sceVideoOutSubmitFlip                flips without a command buffer, so the
 *                                        whole AGC layer goes
 *
 * What is left is: map direct memory, register two linear buffers, let the
 * portable compositor draw STRAIGHT INTO the one that is not on screen, flip.
 * pw_present.h's target is already "a writable linear BGRX surface", which is
 * exactly what a linear VideoOut buffer is — so there is no copy at all, where
 * the PS5 path needs one.
 */
#ifndef PW_VIDEOOUT_PS4_H
#define PW_VIDEOOUT_PS4_H
#include "pw_present.h"
#include <stdint.h>
#include <stddef.h>

enum { PW_VIDEOOUT_PS4_WIDTH = 1920, PW_VIDEOOUT_PS4_HEIGHT = 1080 };
/* 1920 * 1080 * 4, rounded up to the 2 MiB direct-memory alignment. */
#define PW_VIDEOOUT_PS4_FRAME_BYTES 0x800000u
#define PW_VIDEOOUT_PS4_BUFFERS     2

typedef struct PwVideoOutPs4 {
    void    *memory;
    int64_t  physical;
    size_t   bytes, frame_bytes;
    int      handle;
    unsigned opened, allocated, mapped, buffers_registered;
    unsigned next;            /* the buffer to draw into next */
    uint64_t flips;
    /* Kept rather than folded into the status, the way the PS5 original keeps
     * them: a close that half-succeeds is a different bug from one that fails
     * at the first step, and the numbers say which. */
    int unregister_rc, close_rc, munmap_rc, release_rc;
} PwVideoOutPs4;

int pw_videoout_ps4_open(PwVideoOutPs4 *);
int pw_videoout_ps4_close(PwVideoOutPs4 *);

/* Lend the off-screen buffer as a linear BGRX target. */
int pw_videoout_ps4_acquire(PwVideoOutPs4 *, PwPresentTarget *target);
/* Flip to the buffer last acquired, and wait for it to be on screen. */
int pw_videoout_ps4_present(PwVideoOutPs4 *);

/* frame scaled into the whole screen by mode (PW_PRESENT_SCALE_*), the rest
 * background, then flipped. The one call a caller normally wants. */
int pw_videoout_ps4_present_scaled(PwVideoOutPs4 *, const PwPresentFrame *frame,
                                   int mode, uint32_t background);
#endif
