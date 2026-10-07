/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_AUDIO_PS4_H
#define PW_AUDIO_PS4_H
/* The PS4's filling of prospero-win's audio vtable. The port policy itself —
 * pw_audio_ps5_open_port() — is upstream's and is used unchanged. */
#include "pw_audio_ps5.h"
#include "prospero_win.h"

int pw_audio_ps4_platform_ops(PwAudioPs5Ops *);
#endif
