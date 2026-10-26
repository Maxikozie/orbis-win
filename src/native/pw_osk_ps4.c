/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pw_osk_ps4.h"

#include <orbis/ImeDialog.h>
#include <orbis/_types/sysmodule.h>
#include <string.h>

typedef int (*ImeInit)(const OrbisImeDialogSetting *, OrbisImeSettingsExtended *);
typedef OrbisDialogStatus (*ImeGetStatus)(void);
typedef int (*ImeGetResult)(OrbisDialogResult *);
typedef int (*ImeTerm)(void);

extern int sceSysmoduleLoadModule(uint16_t id);
extern int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t *handle);
extern int sceKernelDlsym(int32_t handle, const char *symbol, void **address);

static ImeInit ime_init;
static ImeGetStatus ime_get_status;
static ImeGetResult ime_get_result;
static ImeTerm ime_term;
static uint16_t input_text[129];
static uint16_t dialog_title[64];
static int dialog_active;

static int lookup(int32_t handle, const char *name, void **address)
{
    *address = NULL;
    return sceKernelDlsym(handle, name, address) == 0 && *address != NULL;
}

int pw_osk_ps4_init(void)
{
    int32_t handle = -1;
    int load_rc = sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
    int handle_rc = sceSysmoduleGetModuleHandleInternal(ORBIS_SYSMODULE_IME_DIALOG, &handle);

    if (handle_rc < 0 || handle < 0)
        handle_rc = sceSysmoduleGetModuleHandleInternal(0x80000000u | ORBIS_SYSMODULE_IME_DIALOG, &handle);
    if (handle_rc < 0 || handle < 0) return -1;

    if (!lookup(handle, "sceImeDialogInit", (void **)&ime_init) ||
        !lookup(handle, "sceImeDialogGetStatus", (void **)&ime_get_status) ||
        !lookup(handle, "sceImeDialogGetResult", (void **)&ime_get_result) ||
        !lookup(handle, "sceImeDialogTerm", (void **)&ime_term)) {
        ime_init = NULL;
        return -1;
    }
    (void)load_rc; /* A module may already be loaded by the system. */
    return 0;
}

static void copy_ascii(const char *src, uint16_t *dst, uint32_t cap)
{
    uint32_t i = 0;
    if (cap) {
        while (src && src[i] && i + 1 < cap) {
            dst[i] = (uint8_t)src[i];
            i++;
        }
        dst[i] = 0;
    }
}

int pw_osk_ps4_start(uint32_t user_id, const char *title)
{
    OrbisImeDialogSetting param;
    if (!ime_init || !ime_get_status || !ime_get_result || !ime_term || dialog_active)
        return -1;

    memset(&param, 0, sizeof(param));
    memset(input_text, 0, sizeof(input_text));
    memset(dialog_title, 0, sizeof(dialog_title));
    copy_ascii(title ? title : "Enter text", dialog_title,
               sizeof(dialog_title) / sizeof(dialog_title[0]));
    param.userId = user_id;
    param.type = ORBIS_TYPE_DEFAULT;
    param.supportedLanguages = 0;
    param.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    param.maxTextLength = 128;
    param.inputTextBuffer = (wchar_t *)(void *)input_text;
    param.posx = 960.0f;
    param.posy = 540.0f;
    param.horizontalAlignment = ORBIS_H_CENTER;
    param.verticalAlignment = ORBIS_V_CENTER;
    param.title = (const wchar_t *)(const void *)dialog_title;

    if (ime_init(&param, NULL) < 0) return -1;
    dialog_active = 1;
    return 0;
}

int pw_osk_ps4_poll(uint16_t *text, uint32_t capacity)
{
    OrbisDialogResult result;
    OrbisDialogStatus status;
    uint32_t i;

    if (!dialog_active) return 0;
    status = ime_get_status();
    if (status != ORBIS_DIALOG_STATUS_STOPPED) return 0;

    memset(&result, 0, sizeof(result));
    (void)ime_get_result(&result);
    (void)ime_term();
    dialog_active = 0;
    if (result.endstatus != ORBIS_DIALOG_OK) return -1;

    if (text && capacity) {
        for (i = 0; i + 1 < capacity && i < 128 && input_text[i]; i++)
            text[i] = input_text[i];
        text[i] = 0;
    }
    return 1;
}
