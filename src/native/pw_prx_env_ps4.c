/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Let the title write into THIS module's environment, and publish it.
 *
 * The SDK ships libc only as a static archive, so the title and every PRX each
 * carry a private environment. The title sets WINE_PS5_NTDLL_DIR with its own
 * setenv and Wine, running inside ntdll.prx, reads a different and empty one:
 *
 *     WINE wine: cannot get path to ntdll.so (set WINE_PS5_NTDLL_DIR)
 *
 * even though the title reported environment status=0. So the module exports
 * one function that writes into its own environment and nothing else.
 *
 * ⚠ That is not sufficient on its own. This console's setenv reports success
 * and never publishes `environ`: it stays NULL, with no entries, however many
 * variables are set -- measured, ten calls all returning 0 with environ=0. Wine
 * does not only ask getenv; get_initial_environment walks environ to build the
 * Windows environment block, and walked straight into the NULL:
 *
 *     PW_WINE64 fault signal=0xb addr=0x0 rip=0x800884612   (env.c:877)
 *
 * So keep the list here as well and point environ at it. setenv is still
 * called, so whatever the console's own getenv reads stays in step.
 *
 * Exported through the module's PRXDESC1 descriptor (scripts/prx-ps4.sh), and
 * also the reason setenv is pulled into the module at all -- nothing else in
 * ntdll calls it, so it would otherwise be absent from the static libc copy. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PW_ENV_MAX 64

extern char **environ;
int sceKernelDebugOutText(int channel, const char *text);

static char *pw_env[PW_ENV_MAX + 1];
static unsigned int pw_env_count;

int pw_ps4_setenv(const char *name, const char *value);

int pw_ps4_setenv(const char *name, const char *value)
{
    size_t name_len, len;
    char *entry, line[200];
    unsigned int i;
    int status;

    if (!name || !*name || !value) return -1;
    status = setenv(name, value, 1);

    name_len = strlen(name);
    len = name_len + 1 + strlen(value) + 1;
    if (!(entry = malloc(len))) return status;
    snprintf(entry, len, "%s=%s", name, value);

    for (i = 0; i < pw_env_count; i++) {
        if (!strncmp(pw_env[i], name, name_len) && pw_env[i][name_len] == '=') {
            free(pw_env[i]);
            pw_env[i] = entry;
            entry = NULL;
            break;
        }
    }
    if (entry) {
        if (pw_env_count == PW_ENV_MAX) {
            free(entry);
            sceKernelDebugOutText(0, "[ps4-gladius] module environment is full\n");
            return status;
        }
        pw_env[pw_env_count++] = entry;
        pw_env[pw_env_count] = NULL;
    }
    environ = pw_env;

    snprintf(line, sizeof(line), "[ps4-gladius] module setenv %s status=%d environ=%p entries=%u\n",
             name, status, (void *)environ, pw_env_count);
    sceKernelDebugOutText(0, line);
    return status;
}
