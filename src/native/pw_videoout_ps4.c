/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pw_videoout_ps4.h"
#include "prospero_win.h"
#include <string.h>

#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>

enum {
    WIDTH  = PW_VIDEOOUT_PS4_WIDTH,
    HEIGHT = PW_VIDEOOUT_PS4_HEIGHT,
    FRAME  = PW_VIDEOOUT_PS4_FRAME_BYTES,
    COUNT  = PW_VIDEOOUT_PS4_BUFFERS,
    MEMORY = FRAME * COUNT,
};

/* ⚠ 2 MiB, NOT A PAGE. sceKernelAllocateDirectMemory's alignment argument is
 * what the PS5 original passes (0x200000) and what every PS4 sample passes; a
 * smaller value is accepted and then the scanout reads a misaligned surface. */
enum { DIRECT_ALIGN = 0x200000 };

/* ⚠ THE PIXEL FORMAT IS MEASURED, NOT ASSUMED.
 *
 * MEASURED on hardware 2026-10-07: with A8B8G8R8_SRGB (0x80002200) a source
 * red column reached memory as 00 00 ff ff — correct B,G,R,A from the portable
 * compositor — and the television showed it BLUE. So VideoOut reads byte 0 as
 * red: the memory order it wants is R,G,B,A, exactly as the SDK's name says
 * when read MSB-first.
 *
 * pw_present.c's store_opaque writes B,G,R,A and is portable code we do not
 * get to change. A8R8G8B8 is the format whose byte 0 is blue, which matches it
 * without a swizzle pass over 8 MB per frame.
 *
 * The SDK #defines only A8B8G8R8, so A8R8G8B8's value is named here. If it is
 * rejected, open() falls back to the defined one and records which was used —
 * a wrong guess then costs reversed colours rather than a black screen. */
enum { PIXEL_A8R8G8B8_SRGB = 0x80000000,   /* byte0=B — what the compositor writes */
       PIXEL_A8B8G8R8_SRGB = 0x80002200 }; /* byte0=R — reversed from it */
/* ⚠ TYPE 3 IS WC_GARLIC, NOT WB_ONION — this comment said the opposite until
 * 2026-10-07. The PS4 SDK defines ORBIS_KERNEL_WB_ONION 0x0, WC_GARLIC 0x3,
 * WB_GARLIC 0xA.
 *
 * The value is still the right one and the picture proves it: write-combining
 * suits a surface the CPU only ever writes, sequentially, and the display only
 * ever reads. WB_ONION would be the choice if anything here read the surface
 * back — the compositor does not.
 *
 * Kept as a note rather than quietly corrected, because a constant that works
 * for a reason nobody wrote down is a constant somebody later "fixes". */
enum { DIRECT_TYPE = 3 /* ORBIS_KERNEL_WC_GARLIC */ };

int pw_videoout_ps4_open(PwVideoOutPs4 *video)
{
    if (!video) return PW_ERR_PRECONDITION;
    memset(video, 0, sizeof *video);
    video->handle = -1;
    video->physical = -1;

    /* ⚠ 0xFF (the system user) FIRST, THEN THE SIGNED-IN USER. The PS5 original
     * opens with 0xff and nothing else; on the PS4 some configurations want the
     * real user id instead, and the failure is a negative handle with no other
     * symptom. Trying both costs one call and removes a whole class of "black
     * screen, no reason given" from the first hardware run. */
    video->handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    video->open_user = ORBIS_VIDEO_USER_MAIN;
    if (video->handle < 0) {
        video->open_rc_system = video->handle;
        OrbisUserServiceUserId user = -1;
        if (sceUserServiceInitialize(NULL) >= 0 &&
            sceUserServiceGetInitialUser(&user) >= 0 && user >= 0) {
            video->handle = sceVideoOutOpen(user, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
            video->open_user = (int32_t)user;
        }
    }
    if (video->handle < 0) goto state_failed;

    size_t pool = sceKernelGetDirectMemorySize();
    if (pool < MEMORY) goto limit_failed;

    if (sceKernelAllocateDirectMemory(0, (int64_t)pool, MEMORY, DIRECT_ALIGN,
                                      DIRECT_TYPE, &video->physical) < 0) goto vm_failed;
    video->allocated = 1;

    if (sceKernelMapDirectMemory(&video->memory, MEMORY, 0x33 /* RW */, 0,
                                 video->physical, DIRECT_ALIGN) < 0) goto vm_failed;
    video->mapped = 1;
    video->bytes = MEMORY;
    video->frame_bytes = FRAME;

    /* Start black rather than with whatever the previous title left in this
     * physical memory, which is not guaranteed to be zero. */
    memset(video->memory, 0, MEMORY);

    void *buffers[COUNT];
    for (int i = 0; i < COUNT; i++)
        buffers[i] = (uint8_t *)video->memory + (size_t)i * FRAME;

    (void)sceVideoOutSetFlipRate(video->handle, ORBIS_VIDEO_OUT_FLIP_60HZ);

    static const uint32_t formats[2] = { PIXEL_A8R8G8B8_SRGB, PIXEL_A8B8G8R8_SRGB };
    int registered = 0;
    for (unsigned f = 0; f < 2 && !registered; f++) {
        OrbisVideoOutBufferAttribute attribute;
        memset(&attribute, 0, sizeof attribute);
        sceVideoOutSetBufferAttribute(&attribute, formats[f],
                                      ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                      ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,
                                      WIDTH, HEIGHT, WIDTH /* pixelPitch, in pixels */);
        video->register_rc = sceVideoOutRegisterBuffers(video->handle, 0, buffers,
                                                        COUNT, &attribute);
        if (video->register_rc >= 0) { registered = 1; video->pixel_format = formats[f]; }
    }
    if (!registered) goto state_failed;
    video->buffers_registered = 1;
    video->opened = 1;

    /* ⚠ WITHOUT THIS THE SPLASH SCREEN STAYS ON TOP and every frame below it is
     * invisible — which presents as "the title ran and nothing happened", the
     * most expensive symptom to diagnose on a console that has to be rebooted
     * to retry. The PS5 original calls it for the same reason; the PS4 SDK
     * exports it too. Return value ignored: it fails harmlessly when there is
     * no splash left to hide. */
    (void)sceSystemServiceHideSplashScreen();
    return PW_OK;

limit_failed:
    (void)pw_videoout_ps4_close(video);
    return PW_ERR_LIMIT;
vm_failed:
    (void)pw_videoout_ps4_close(video);
    return PW_ERR_VM;
state_failed:
    (void)pw_videoout_ps4_close(video);
    return PW_ERR_STATE;
}

int pw_videoout_ps4_close(PwVideoOutPs4 *video)
{
    if (!video) return PW_ERR_PRECONDITION;
    int status = PW_OK;
    video->unregister_rc = video->close_rc = video->munmap_rc = video->release_rc = 0;

    if (video->buffers_registered) {
        video->unregister_rc = sceVideoOutUnregisterBuffers(video->handle, 0);
        if (video->unregister_rc) status = PW_ERR_STATE;
        else video->buffers_registered = 0;
    }
    if (video->handle >= 0) {
        video->close_rc = sceVideoOutClose(video->handle);
        if (video->close_rc) status = PW_ERR_STATE;
        else video->handle = -1;
    }
    video->opened = 0;
    if (video->mapped) {
        video->munmap_rc = sceKernelMunmap(video->memory, MEMORY);
        if (video->munmap_rc) status = PW_ERR_STATE;
        else video->mapped = 0;
    }
    if (video->allocated) {
        video->release_rc = sceKernelReleaseDirectMemory(video->physical, MEMORY);
        if (video->release_rc) status = PW_ERR_STATE;
        else video->allocated = 0;
    }
    video->memory = NULL;
    video->physical = -1;
    video->bytes = video->frame_bytes = 0;
    return status;
}

int pw_videoout_ps4_acquire(PwVideoOutPs4 *video, PwPresentTarget *target)
{
    if (!video || !target) return PW_ERR_PRECONDITION;
    if (!video->opened) return PW_ERR_STATE;

    /* ⚠ DO NOT HAND OUT A BUFFER THE DISPLAY MAY STILL BE SCANNING.
     *
     * The first version flipped and then waited one vblank, with two buffers.
     * On hardware that flickered: composing 1920x1080x4 with the CPU does not
     * reliably finish inside a frame, so the flip lands late and the next
     * compose starts writing the buffer still on screen. A vblank is not the
     * same event as a flip retiring.
     *
     * Three buffers and a wait on numFlipPending fixes both halves: the extra
     * buffer absorbs a compose that overruns, and the status read is the actual
     * question ("has the display let go of this one") rather than a proxy. */
    for (int spin = 0; spin < 600; spin++) {
        OrbisVideoOutFlipStatus status;
        if (sceVideoOutGetFlipStatus(video->handle, &status) < 0) break;
        if (status.numFlipPending < (int32_t)(COUNT - 1)) break;
        sceVideoOutWaitVblank();
    }

    *target = (PwPresentTarget){
        .pixels = (uint8_t *)video->memory + (size_t)video->next * FRAME,
        .width  = WIDTH,
        .height = HEIGHT,
        .stride = WIDTH * 4u,
        .bytes  = (uint64_t)WIDTH * HEIGHT * 4u,
    };
    return PW_OK;
}

int pw_videoout_ps4_present(PwVideoOutPs4 *video)
{
    if (!video) return PW_ERR_PRECONDITION;
    if (!video->opened) return PW_ERR_STATE;

    /* ⚠ FLIP ARG IS A USER VALUE, NOT A BUFFER INDEX. The index is argument 2;
     * argument 4 comes back through sceVideoOutGetFlipStatus so a caller can
     * tell which flip retired. Passing the index in both is harmless and
     * useful, so the sequence is readable in a flip status dump. */
    if (sceVideoOutSubmitFlip(video->handle, (int32_t)video->next,
                              ORBIS_VIDEO_OUT_FLIP_VSYNC,
                              (int64_t)video->flips) < 0)
        return PW_ERR_STATE;

    video->flips++;
    video->next = (video->next + 1u) % COUNT;
    /* The wait moved into acquire(), which is where the question actually
     * matters — blocking here would stall the caller even when a free buffer
     * is already available. */
    return PW_OK;
}

int pw_videoout_ps4_present_scaled(PwVideoOutPs4 *video, const PwPresentFrame *frame,
                                   int mode, uint32_t background)
{
    PwPresentTarget target;
    int status = pw_videoout_ps4_acquire(video, &target);
    if (status != PW_OK) return status;

    PwPresentPlacement placement;
    status = pw_present_scale(frame, mode, background, &target, &placement);
    if (status != PW_OK) return status;

    return pw_videoout_ps4_present(video);
}
