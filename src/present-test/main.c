/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* First prospero-win frame on a PlayStation 4.
 *
 * Draws three colour bars through the UNMODIFIED portable compositor
 * (pw_present_scale) into a linear VideoOut buffer, and flips.
 *
 * ⚠ IT IS A COLOUR TEST ON PURPOSE. pw_present.c's store_opaque writes bytes
 * B,G,R,A — a BGRX surface. The PS4 SDK names its only VideoOut format
 * A8B8G8R8_SRGB, which read MSB-first is the OPPOSITE byte order. The PS5
 * original uses the same 0x2200 format family and calls it "B8G8R8A8", so the
 * naming convention is doing the lying somewhere, and headers cannot settle it.
 * Three unambiguous bars can: whoever is watching the television says which
 * colour is on the left, and the answer is one line of code either way.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <orbis/libkernel.h>

#include "pw_videoout_ps4.h"
#include "pw_pad_ps4.h"
#include "pw_pad_ps5.h"
#include "pw_present.h"
#include "prospero_win.h"

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
    fputs(buf, stdout);
    fflush(stdout);
}

/* A BGRX pixel as pw_present.c lays it down: byte0=B, byte1=G, byte2=R. */
#define BGRX(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

enum { SRC_W = 320, SRC_H = 180 };

int main(void)
{
    say("=== prospero-win presenter, PS4 ===");

    static uint32_t pixels[SRC_W * SRC_H];
    for (uint32_t y = 0; y < SRC_H; y++) {
        for (uint32_t x = 0; x < SRC_W; x++) {
            uint32_t c;
            if      (x < SRC_W / 3)     c = BGRX(255, 0, 0);   /* expect RED   left   */
            else if (x < 2 * SRC_W / 3) c = BGRX(0, 255, 0);   /* expect GREEN middle */
            else                        c = BGRX(0, 0, 255);   /* expect BLUE  right  */
            /* A white bar along the top edge: proves orientation and stride,
             * which a left/right colour question alone would not. */
            if (y < 8) c = BGRX(255, 255, 255);
            pixels[y * SRC_W + x] = c;
        }
    }

    PwPresentFrame frame = {
        .pixels = (const uint8_t *)pixels,
        .width  = SRC_W,
        .height = SRC_H,
        .stride = SRC_W * 4u,
        .format = PW_PRESENT_BGRX8,
    };
    if (pw_present_validate(&frame) != PW_OK) {
        say("present: frame rejected by pw_present_validate");
        for (;;) sleep(1);
    }
    say("present: source %ux%u BGRX8, validated by the portable code", SRC_W, SRC_H);

    PwVideoOutPs4 video;
    int status = pw_videoout_ps4_open(&video);
    say("present: pw_videoout_ps4_open -> %d (handle %d)", status, video.handle);
    if (status != PW_OK) {
        say("present: open failed - rc unregister=%d close=%d munmap=%d release=%d",
            video.unregister_rc, video.close_rc, video.munmap_rc, video.release_rc);
        for (;;) sleep(1);
    }
    say("present: %zu bytes mapped, %u buffers of %zu",
        video.bytes, PW_VIDEOOUT_PS4_BUFFERS, video.frame_bytes);
    say("present: opened with user 0x%x%s", (unsigned)video.open_user,
        video.open_rc_system ? " (0xFF was refused, fell back)" : "");
    say("present: pixel format 0x%08x %s (register rc %d)", video.pixel_format,
        video.pixel_format == 0x80000000u ? "A8R8G8B8 - byte0=B, matches the compositor"
                                          : "A8B8G8R8 - byte0=R, colours will be REVERSED",
        video.register_rc);

    /* ⚠ READ THE SURFACE BACK BEFORE TRUSTING THE SCREEN. A black television
     * is the same picture whether the compositor wrote nothing, wrote the
     * wrong thing, or wrote correctly into memory the display never scans. One
     * dump separates the first from the other two, and costs one frame. */
    {
        PwPresentTarget probe_target;
        if (pw_videoout_ps4_acquire(&video, &probe_target) == PW_OK) {
            PwPresentPlacement placement;
            int rc = pw_present_scale(&frame, PW_PRESENT_SCALE_INTEGER,
                                      BGRX(24, 24, 32), &probe_target, &placement);
            say("present: compose -> %d, placed %ux%u at %u,%u scale %u", rc,
                placement.shown_width, placement.shown_height,
                placement.left, placement.top, placement.scale);
            const uint8_t *px = probe_target.pixels;
            /* Row 0 is the dark surround; the row the white bar lands on is
             * placement.top. Both are named so a wrong one is obvious. */
            say("present: surround px[0] = %02x %02x %02x %02x (expect 20 18 18 ff)",
                px[0], px[1], px[2], px[3]);
            const uint8_t *bar = px + (size_t)placement.top * probe_target.stride
                                    + (size_t)placement.left * 4u;
            say("present: white-bar px   = %02x %02x %02x %02x (expect ff ff ff ff)",
                bar[0], bar[1], bar[2], bar[3]);
            /* One quarter into the shown image is inside the RED column. */
            const uint8_t *red = px + ((size_t)placement.top + 40) * probe_target.stride
                                    + ((size_t)placement.left + 40) * 4u;
            say("present: red-column px  = %02x %02x %02x %02x", red[0], red[1], red[2], red[3]);
            say("present:   red as B,G,R,A is 00 00 ff ff; VideoOut must read byte0 as BLUE");
            say("present:   for that to show red. Format in use is logged above.");
        }
    }

    /* ---------------------------------------------------------------- pad
     * The core is upstream's native/pw_pad_ps5.c, unmodified; only the vtable
     * underneath it is ours. If the DualShock drives this, the batching,
     * generation tracking and edge detection above it are working too. */
    PwPadPs5 pad;
    PwPadPs5Ops ops;
    int pad_ready = 0;
    if (pw_pad_ps4_platform_ops(&ops) == PW_OK &&
        pw_pad_ps5_open(&pad, &ops, NULL, 0) == PW_OK) {
        pad_ready = 1;
        say("pad: open, user 0x%x handle %d", (unsigned)pad.user_id, pad.pad_handle);
    } else {
        say("pad: NOT open (init rc %d, user rc %d) - the picture still runs",
            pad.pad_init_rc, pad.user_initialize_rc);
    }

    /* 320x180 at 6x is 1920x1080, but INTEGER picks the largest fit with the
     * margin, so a surround is left for the pad to tint. */
    uint32_t background = BGRX(24, 24, 32);
    uint64_t reported = 0;

    for (int i = 0; i < 3600; i++) {
        if (pad_ready && pw_pad_ps5_read(&pad) == PW_OK) {
            /* previous_buttons is the state after the newest tracked sample — the
             * core keeps no separate "held" field. */
            uint32_t held = pad.core.previous_buttons;
            /* Face buttons pick a surround colour; the analogue triggers dim
             * it. Every one of those is a different field arriving intact. */
            if      (held & 0x4000u) background = BGRX(120, 24, 24);   /* CROSS    */
            else if (held & 0x2000u) background = BGRX(24, 110, 40);   /* CIRCLE   */
            else if (held & 0x8000u) background = BGRX(30, 40, 130);   /* SQUARE   */
            else if (held & 0x1000u) background = BGRX(120, 100, 24);  /* TRIANGLE */
            else                     background = BGRX(24, 24, 32);
            uint32_t dim = (uint32_t)pad.l2 + (uint32_t)pad.r2;
            if (dim) {
                uint32_t b = (uint32_t)(background & 0xffu) * (510u - dim) / 510u;
                uint32_t g = ((background >> 8) & 0xffu) * (510u - dim) / 510u;
                uint32_t r = ((background >> 16) & 0xffu) * (510u - dim) / 510u;
                background = BGRX(r, g, b);
            }
            if (pad.core.pressed_edges && reported < 20) {
                say("pad: pressed 0x%08x held 0x%08x L(%3u,%3u) R(%3u,%3u) L2=%3u R2=%3u",
                    pad.core.pressed_edges, held,
                    pad.left_stick.x, pad.left_stick.y,
                    pad.right_stick.x, pad.right_stick.y, pad.l2, pad.r2);
                reported++;
            }
        }
        status = pw_videoout_ps4_present_scaled(&video, &frame,
                                                PW_PRESENT_SCALE_INTEGER, background);
        if (status != PW_OK) { say("present: frame %d -> %d", i, status); break; }
        if (i == 0 || i == 59 || i == 599)
            say("present: %d frames, %llu flips", i + 1, (unsigned long long)video.flips);
    }

    if (pad_ready) {
        say("pad: %llu polls, %llu connected, %llu empty, %llu errors, %llu generations",
            (unsigned long long)pad.polls, (unsigned long long)pad.connected_samples,
            (unsigned long long)pad.empty_reads, (unsigned long long)pad.read_errors,
            (unsigned long long)pad.generation_changes);
        (void)pw_pad_ps5_close(&pad);
    }

    say("");
    say("WHAT SHOULD BE ON SCREEN:");
    say("  a white bar along the TOP, and below it three columns,");
    say("  left to right: RED, GREEN, BLUE, on a dark grey surround.");
    say("  Hold CROSS/CIRCLE/SQUARE/TRIANGLE to tint the surround,");
    say("  and squeeze L2/R2 to dim it. That is upstream's pad core running");
    say("  on a DualShock 4 through a vtable this port supplies.");
    say("");
    say("present: idling so the log can be read; close from the PS button menu");
    for (;;) sleep(1);
    return 0;
}
