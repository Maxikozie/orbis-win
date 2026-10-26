/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_NETCTL_PS4_H
#define PW_NETCTL_PS4_H

#include <stddef.h>

/* Query the console's IPv4 configuration and format it as
 * address,netmask,gateway,dns1,dns2,mtu. */
int pw_ps4_netctl_config(char *buffer, size_t size);

#endif
