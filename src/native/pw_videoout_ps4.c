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
/* Direct-memory type 3 is WB_ONION on both consoles: CPU-written, GPU/display
 * readable without an explicit flush. WC_GARLIC (type 0) is faster for the GPU
 * and much slower for the CPU writes the compositor does, which is the whole
 * workload here. */
enum { DIRECT_TYPE = 3 };

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

    OrbisVideoOutBufferAttribute attribute;
    memset(&attribute, 0, sizeof attribute);
    sceVideoOutSetBufferAttribute(&attribute,
                                  ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                  ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,
                                  WIDTH, HEIGHT, WIDTH /* pixelPitch */);

    (void)sceVideoOutSetFlipRate(video->handle, ORBIS_VIDEO_OUT_FLIP_60HZ);

    if (sceVideoOutRegisterBuffers(video->handle, 0, buffers, COUNT, &attribute) < 0)
        goto state_failed;
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

    /* Synchronous, as pw_present.h's sink contract requires: when this returns
     * the lent target may be reused. With two buffers the wait is what stops
     * the next frame being drawn into the one still on screen. */
    sceVideoOutWaitVblank();
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
