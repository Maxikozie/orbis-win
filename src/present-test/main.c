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

enum { SRC_W = 480, SRC_H = 270 };

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

    /* PW_PRESENT_SCALE_INTEGER: 480x270 into 1920x1080 is exactly 4x, so any
     * softness or tearing on screen is the presenter's doing and not the
     * scaler's. */
    for (int i = 0; i < 600; i++) {
        status = pw_videoout_ps4_present_scaled(&video, &frame,
                                                PW_PRESENT_SCALE_INTEGER, BGRX(24, 24, 32));
        if (status != PW_OK) { say("present: frame %d -> %d", i, status); break; }
        if (i == 0 || i == 1 || i == 59 || i == 599)
            say("present: %d frames, %llu flips", i + 1, (unsigned long long)video.flips);
    }

    say("");
    say("WHAT SHOULD BE ON SCREEN:");
    say("  a white bar along the TOP, and below it three columns,");
    say("  left to right: RED, GREEN, BLUE, on a dark grey surround.");
    say("  If left is BLUE and right is RED, the channel order is reversed");
    say("  and the fix is one line in pw_videoout_ps4.c's format argument.");
    say("");
    say("present: idling so the log can be read; close from the PS button menu");
    for (;;) sleep(1);
    return 0;
}
