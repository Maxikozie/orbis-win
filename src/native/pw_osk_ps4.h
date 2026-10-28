/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_OSK_PS4_H
#define PW_OSK_PS4_H

#include <stdint.h>

/* Loads the Orbis IME dialog module and resolves its entry points. Safe to call
 * when the system module is unavailable; start() then reports failure. */
int pw_osk_ps4_init(void);
int pw_osk_ps4_start(uint32_t user_id, const char *title);
/* Returns 1 when a dialog completes, 0 while it is running, -1 on cancel or
 * error. The dialog's UTF-16 output is copied to text on completion. */
int pw_osk_ps4_poll(uint16_t *text, uint32_t capacity);

#endif
