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
#include "pw_audio_ps4.h"
#include "pw_wine_dl_ps4.h"
#include "pw_wine_dmem_ps4.h"
#include <math.h>

/* One 16 KiB page, for the zero-fill probe. */
#define PW_WINE_DMEM_PAGE_PROBE 0x4000u
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

/* ⚠ DELIBERATELY NOT AN EXACT DIVISOR OF 1920x1080. 480x270 (4x) and 320x180
 * (6x) both fill the screen exactly, leaving no surround — which made the pad
 * tint invisible and looked like the pad was broken. 300x170 scales 6x to
 * 1800x1020 and leaves a border on every side. */
enum { SRC_W = 300, SRC_H = 170 };

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

    /* --------------------------------------------------------- direct memory
     * Wine wants far more anonymous memory than a title's flexible pool has,
     * so ntdll's VM manager is routed onto direct memory instead. This is the
     * machinery that does it, driven end to end: own a region, commit pages
     * into it, write and read them back, then give them away.
     *
     * ⚠ THE READ-BACK IS THE POINT. reserve/allocate/map all returning 0 only
     * says the kernel accepted the calls. Writing a pattern and reading it
     * through the same addresses is what proves the physical memory actually
     * landed under them — a map at the wrong offset returns success and hands
     * back somebody else's page. */
    {
        static PwWineDmemRun runs[256];
        PwWineDmem dmem;
        PwWineDmemOps dops;
        if (pw_wine_dmem_ps4_ops(&dops) == PW_OK &&
            pw_wine_dmem_init(&dmem, &dops, runs, 256, 1) == 0) {

            /* 16 MiB of address space high enough not to collide with the
             * eboot (0x400000..0x544000) or anything the SDK maps low. */
            const uintptr_t base = (uintptr_t)0x40000000;
            const size_t    span = 16u << 20;
            int rc = pw_wine_dmem_add_region(&dmem, base, span, 0);
            say("dmem: add_region(%p, %zu) -> %d", (void *)base, span, rc);

            if (rc == 0) {
                const size_t commit = 256u << 10;      /* 256 KiB, 16 pages */
                rc = pw_wine_dmem_protect(&dmem, base, commit, PW_WINE_DMEM_READ | PW_WINE_DMEM_WRITE);
                say("dmem: commit %zu bytes RW -> %d, backed %zu",
                    commit, rc, pw_wine_dmem_backed(&dmem, base, commit));

                if (rc == 0) {
                    volatile uint32_t *p = (volatile uint32_t *)base;
                    const uint32_t n = (uint32_t)(commit / sizeof *p);
                    for (uint32_t i = 0; i < n; i++) p[i] = i * 2654435761u;
                    uint32_t bad = 0;
                    for (uint32_t i = 0; i < n; i++)
                        if (p[i] != i * 2654435761u) bad++;
                    say("dmem: wrote and verified %u words, %u mismatched", n, bad);

                    /* Zero-fill was requested at init, so a freshly backed
                     * page must read zero before anything writes it. */
                    volatile uint32_t *q = (volatile uint32_t *)(base + commit);
                    rc = pw_wine_dmem_protect(&dmem, base + commit, PW_WINE_DMEM_PAGE_PROBE,
                                              PW_WINE_DMEM_READ | PW_WINE_DMEM_WRITE);
                    if (rc == 0) say("dmem: fresh page reads 0x%08x (expect 0)", q[0]);
                }

                PwWineDmemStats st;
                pw_wine_dmem_stats(&dmem, &st);
                say("dmem: backed %llu peak %llu, %llu allocs %llu maps %llu protects %llu failures, %u runs",
                    (unsigned long long)st.backed_bytes, (unsigned long long)st.peak_backed_bytes,
                    (unsigned long long)st.allocations, (unsigned long long)st.maps,
                    (unsigned long long)st.protects, (unsigned long long)st.failures, st.runs);

                /* ⚠ replace(), NOT protect(). A first run called
                 * pw_wine_dmem_protect(.., 0) expecting it to free, and got
                 * rc 0 with backed_bytes unchanged and releases still 0 —
                 * which read like a leak in the port and was a misread of the
                 * API. protect(.., 0) declines to BACK reserved pages; it is
                 * replace() whose header says "protection 0 leaves it
                 * reserved", and which releases the direct memory behind it. */
                rc = pw_wine_dmem_replace(&dmem, base, span, 0);
                pw_wine_dmem_stats(&dmem, &st);
                say("dmem: replace(..,0) -> %d, backed now %llu, %llu releases",
                    rc, (unsigned long long)st.backed_bytes, (unsigned long long)st.releases);
            }
        } else {
            say("dmem: init refused");
        }
    }

    /* ------------------------------------------------------- module loader
     * ⚠ THIS CHECKS AN ARITHMETIC CLAIM AGAINST THE CONSOLE.
     *
     * pw_prx_parse_module_info() hardcodes PS5 offsets — name at 8, four
     * segments at 0x108, their count at 0x148, 0x160 bytes total. The PS4
     * SDK's OrbisKernelModuleInfo computes to exactly the same layout, which
     * is why the parser is used unmodified. But "computes to" is a claim about
     * a header, and the kernel is the authority.
     *
     * So: ask the kernel about a module, hand the raw bytes to upstream's
     * parser, and see whether a sane name and plausible segments come back. A
     * wrong offset here would not error — it would silently read segment
     * addresses from the wrong place and the descriptor scan would later fail
     * looking like a missing descriptor. */
    {
        PwWineDlOps dlops;
        if (pw_wine_dl_ps4_ops(&dlops, NULL) == PW_OK) {
            uint8_t info[PW_PRX_MODULE_INFO_BYTES];
            memset(info, 0, sizeof info);
            *(uint64_t *)info = PW_PRX_MODULE_INFO_BYTES;   /* size, as the ABI wants */

            int rc = dlops.module_info(0, info);            /* 0 = this eboot */
            say("dl: sceKernelGetModuleInfo(0) -> %d", rc);
            if (rc >= 0) {
                char name[PW_PRX_MAX_NAME];
                PwPrxSegment segs[PW_PRX_MAX_SEGMENTS];
                uint32_t nseg = 0;
                int prc = pw_prx_parse_module_info(info, name, segs, &nseg);
                say("dl: pw_prx_parse_module_info -> %d, name '%s', %u segment(s)",
                    prc, prc == PW_PRX_OK ? name : "?", nseg);
                for (uint32_t i = 0; i < nseg && i < PW_PRX_MAX_SEGMENTS; i++)
                    say("dl:   seg %u  %p  %u bytes  prot 0x%x",
                        i, segs[i].address, segs[i].size, segs[i].protection);
                say("dl:   a sane name and a text segment here means the PS5");
                say("dl:   offsets match this console and the parser is portable");
            }
        } else {
            say("dl: ops refused");
        }
    }

    /* -------------------------------------------------------------- audio
     * A one-second tone before the video loop, deliberately NOT interleaved
     * with it: sceAudioOutOutput blocks until the port takes the grain, and a
     * 256-frame grain at 48 kHz is 5.33 ms against a 16.6 ms frame. Pushing one
     * grain per frame would starve the port and stutter; pushing three would
     * couple the render loop's timing to the audio clock. Neither belongs in a
     * test whose job is to prove the port opens and plays.
     *
     * The port policy — main port, 256-frame grains, S16 stereo at 48 kHz,
     * unity volume on eight channels — is upstream's pw_audio_ps5_open_port(),
     * unchanged. Only the five forwards under it are this port's. */
    {
        PwAudioPs5Ops aops;
        int ahandle = -1;
        int arc = pw_audio_ps4_platform_ops(&aops);
        if (arc == PW_OK) arc = pw_audio_ps5_open_port(&aops, &ahandle);
        if (arc == PW_OK) {
            say("audio: port %d open, %u frames per grain at %u Hz, S16 stereo",
                ahandle, PW_AUDIO_PS5_GRAIN, PW_AUDIO_PS5_RATE);
            static int16_t grain[PW_AUDIO_PS5_GRAIN * PW_AUDIO_PS5_CHANNELS];
            const double step = 2.0 * 3.14159265358979 * 440.0 / PW_AUDIO_PS5_RATE;
            double phase = 0.0;
            int grains = PW_AUDIO_PS5_RATE / PW_AUDIO_PS5_GRAIN;   /* ~one second */
            int failed = 0;
            for (int g = 0; g < grains && !failed; g++) {
                for (unsigned f = 0; f < PW_AUDIO_PS5_GRAIN; f++) {
                    /* A quarter of full scale: loud enough to hear, quiet
                     * enough not to startle whoever is holding the pad. */
                    int16_t v = (int16_t)(8000.0 * sin(phase));
                    phase += step;
                    grain[f * 2] = grain[f * 2 + 1] = v;
                }
                if (aops.output(ahandle, grain) < 0) {
                    say("audio: output failed at grain %d of %d", g, grains);
                    failed = 1;
                }
            }
            if (!failed) say("audio: %d grains played - a 440 Hz tone, about a second", grains);
            (void)aops.close(ahandle);
        } else {
            say("audio: NOT open (rc %d) - the rest still runs", arc);
        }
    }

    /* ---------------------------------------------------------------- pad
     * The core is upstream's native/pw_pad_ps5.c, unmodified; only the vtable
     * underneath it is ours. If the DualShock drives this, the batching,
     * generation tracking and edge detection above it are working too. */
    /* ⚠ pw_pad_init REFUSES A NULL MAP: `if(!pad || !map || !count ...) return
     * PW_ERR_PRECONDITION`. Passing NULL,0 made pw_pad_ps5_open return before
     * it touched a single platform call, and the failure then printed
     * pad_init_rc and user_initialize_rc — both still zero from the memset,
     * which read like the platform had succeeded. A map is not optional. */
    static const PwPadKeyMap keymap[] = {
        { 0x4000u, 0x0D, 0x1C, 0, "accept" },   /* CROSS    -> VK_RETURN */
        { 0x2000u, 0x1B, 0x01, 0, "cancel" },   /* CIRCLE   -> VK_ESCAPE */
        { 0x8000u, 0x20, 0x39, 0, "space"  },   /* SQUARE   -> VK_SPACE  */
        { 0x1000u, 0x09, 0x0F, 0, "tab"    },   /* TRIANGLE -> VK_TAB    */
        { 0x0010u, 0x26, 0x48, 1, "up"     },
        { 0x0040u, 0x28, 0x50, 1, "down"   },
        { 0x0080u, 0x25, 0x4B, 1, "left"   },
        { 0x0020u, 0x27, 0x4D, 1, "right"  },
    };

    PwPadPs5 pad;
    PwPadPs5Ops ops;
    int pad_ready = 0;
    int ops_rc = pw_pad_ps4_platform_ops(&ops);
    int open_rc = (ops_rc == PW_OK)
                ? pw_pad_ps5_open(&pad, &ops, keymap, sizeof keymap / sizeof keymap[0])
                : PW_ERR_PRECONDITION;
    if (open_rc == PW_OK) {
        pad_ready = 1;
        say("pad: open, user 0x%x handle %d", (unsigned)pad.user_id, pad.pad_handle);
    } else {
        /* Every step, so the failing one names itself rather than being
         * inferred from two zeroes. */
        say("pad: NOT open - ops_rc=%d open_rc=%d user_id=%d handle=%d",
            ops_rc, open_rc, pad.user_id, pad.pad_handle);
        say("pad:   user_init_rc=%d pad_init_rc=%d terminate_rc=%d",
            pad.user_initialize_rc, pad.pad_init_rc, pad.terminate_rc);
        say("pad:   the picture still runs");
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
