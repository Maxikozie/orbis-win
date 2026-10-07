/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Binds prospero-win's platform contract to this port's PS4 implementations.
 *
 * The title (native/wine64_main.c) calls the pw_*_ps5_* entry points. Our
 * implementations are named pw_*_ps4_* because they were written as standalone
 * titles first (presenter, probe), so this file is the join: upstream's cores
 * are compiled with their *_HOST_TEST guards, which omit the PS5 vtables, and
 * the vtable entry points land here forwarding to ours.
 *
 * Nothing here reimplements anything -- it is one hop per symbol. Where this
 * port has no implementation yet (HID, the Lapy elevation helper) the entry
 * point is an explicit, logged no-op rather than a missing symbol, so the title
 * starts and says what it is missing instead of failing to link.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pw_videoout_ps5.h"
#include "pw_pad_ps5.h"
#include "pw_audio_ps5.h"
#include "pw_hid_ps5.h"

/* ---- our PS4 implementations ---- */
#include "pw_videoout_ps4.h"
#include "pw_present.h"
int pw_pad_ps4_platform_ops(PwPadPs5Ops *);
int pw_audio_ps4_platform_ops(PwAudioPs5Ops *);

/* ---- the contract the title links against ---- */
/* ---- video out ----
 * The contract's PwVideoOutPs5 carries PwAgcPs5 state, which this port has no
 * equivalent for: the PS5 draws a TILED frame and has AGC blit it into scanout,
 * while the PS4 scans out LINEAR and flips with sceVideoOutSubmitFlip, so the
 * tiling tables and the whole AGC layer are gone. The device state therefore
 * stays ours and the caller's struct is only a handle -- it is opaque to the
 * title, which never reads a field.
 *
 * That also collapses the contract's four entry points onto two of ours: the
 * PS5 present() hand-rolls layout, background fill and a scaled blit through
 * the tiling tables, where the linear path is exactly what the portable
 * compositor already writes, so present() is frame_from_view + present_scaled. */
static PwVideoOutPs4 g_videoout;

#define PW_BIND_BACKGROUND 0xff181010u   /* the PS5 path's background */

int pw_videoout_ps5_open(PwVideoOutPs5 *v)
{
    if (v) memset(v, 0, sizeof(*v));
    return pw_videoout_ps4_open(&g_videoout);
}

int pw_videoout_ps5_close(PwVideoOutPs5 *v)
{
    (void)v;
    return pw_videoout_ps4_close(&g_videoout);
}

int pw_videoout_ps5_present(PwVideoOutPs5 *v, const PwPresentView *view)
{
    PwPresentFrame frame;
    int status;
    (void)v;
    status = pw_present_frame_from_view(view, &frame);
    if (status != PW_OK) return status;
    /* INTEGER matches the PS5 path, which scales by a whole factor and shows
     * background in the margins rather than stretching. */
    return pw_videoout_ps4_present_scaled(&g_videoout, &frame,
                                          PW_PRESENT_SCALE_INTEGER, PW_BIND_BACKGROUND);
}

int pw_videoout_ps5_present_scaled(PwVideoOutPs5 *v, const PwPresentFrame *frame,
                                   int mode, uint32_t background)
{
    (void)v;
    return pw_videoout_ps4_present_scaled(&g_videoout, frame, mode, background);
}

/* Draw into the caller's buffer without showing it. On PS5 this exists so a
 * frame can be prepared in the Vulkan driver's B8G8R8A8 order before that
 * driver owns the video output; linear scanout means the same bytes, so the
 * portable compositor writes straight into a target laid over `out`. */
int pw_videoout_ps5_draw_scaled(const PwPresentFrame *frame, int mode, uint32_t background,
                                uint32_t *out)
{
    PwPresentTarget target;
    PwPresentPlacement placement;
    if (!frame || !out) return PW_ERR_PRECONDITION;
    target.pixels = (uint8_t *)out;
    target.width  = PW_VIDEOOUT_PS5_WIDTH;
    target.height = PW_VIDEOOUT_PS5_HEIGHT;
    target.stride = PW_VIDEOOUT_PS5_WIDTH * 4u;
    target.bytes  = PW_VIDEOOUT_PS5_FRAME_BYTES;
    return pw_present_scale(frame, mode, background, &target, &placement);
}
int pw_pad_ps5_platform_ops(PwPadPs5Ops *ops) { return pw_pad_ps4_platform_ops(ops); }
int pw_audio_ps5_platform_ops(PwAudioPs5Ops *ops) { return pw_audio_ps4_platform_ops(ops); }

/* ---- not ported yet ----
 * HID is keyboard and mouse over USB; the DualShock 4 arrives through the pad
 * path, which IS ported, so a title is playable without this. Returning no ops
 * is the same answer the PS5 build gives when no HID device is present. */
const PwHidPs5Ops *pw_hid_ps5_system_ops(void (*log)(const char *line))
{
    if (log) log("hid: no PS4 implementation, continuing without keyboard or mouse");
    return NULL;
}
void pw_hid_ps5_preload(void (*log)(const char *line))
{
    (void)log;   /* nothing to preload without a backend */
}

/* The Lapy elevation helper is a PS5 privilege-escalation path for the WebKit
 * host process. A PS4 title under GoldHEN already runs with the privileges it
 * needs, so there is nothing to request. */
int pw_lapy_elevation_request(int32_t pid)
{
    (void)pid;
    return 0;
}
