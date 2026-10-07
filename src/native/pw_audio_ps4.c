/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The PS4's filling of prospero-win's audio vtable.
 *
 * The third platform piece, and the smallest. native/pw_audio_ps5.c guards its
 * PS5 forwards behind PW_AUDIO_PS5_HOST_TEST exactly as the pad does, so the
 * policy above them — open the main port, 256-frame grains of 16-bit stereo at
 * 48 kHz, unity volume on all eight channels, close on any failure — is
 * upstream code compiled unmodified.
 *
 * ⚠ AND THE CONSTANTS ARE THE SAME ON BOTH CONSOLES, which is why nothing here
 * translates: PORT_MAIN 0, FORMAT_S16_STEREO 1, the 0xff system user, and
 * 0x8000 for 0 dB all match ORBIS_AUDIO_OUT_* in the PS4 SDK. The PS5 header
 * names them privately rather than importing them, so this was worth checking
 * rather than assuming — a wrong format constant is silence, not an error.
 */
#include "pw_audio_ps4.h"

#include <orbis/AudioOut.h>
#include <orbis/UserService.h>

static int platform_init(void)
{
    /* Once per process, as upstream's does: sceAudioOutInit refuses a second
     * call and the refusal would read as a failure to open. */
    static int initialized, result;
    if (!initialized) { result = sceAudioOutInit(); initialized = result >= 0; }
    return result;
}

static int platform_open(int user, int type, int index, uint32_t grain,
                         uint32_t rate, uint32_t format)
{
    return sceAudioOutOpen((OrbisUserServiceUserId)user,
                           (enum OrbisAudioOutPort)type, index, grain, rate, format);
}

static int platform_volume(int handle, int flags, const int32_t *volumes)
{
    /* The PS4 declares the array non-const; the vtable passes it const. Cast at
     * the one place that knows both rather than loosening the vtable. */
    return sceAudioOutSetVolume(handle, flags, (int32_t *)volumes);
}

static int platform_output(int handle, const void *samples)
{
    return sceAudioOutOutput(handle, samples);
}

static int platform_close(int handle) { return sceAudioOutClose(handle); }

int pw_audio_ps4_platform_ops(PwAudioPs5Ops *ops)
{
    if (!ops) return PW_ERR_PRECONDITION;
    *ops = (PwAudioPs5Ops){
        .init   = platform_init,
        .open   = platform_open,
        .volume = platform_volume,
        .output = platform_output,
        .close  = platform_close,
    };
    return PW_OK;
}
