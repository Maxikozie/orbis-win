/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pw_netctl_ps4.h"

#include <stdio.h>
#include <string.h>
#include <orbis/NetCtl.h>

static int get_info(int id, OrbisNetCtlInfo *info, const char **value)
{
    int status = sceNetCtlGetInfo(id, info);
    if (status < 0) return status;
    *value = info->ip_address;
    return 0;
}

int pw_ps4_netctl_config(char *buffer, size_t size)
{
    OrbisNetCtlInfo ip, mask, route, dns1, dns2, mtu;
    const char *ip_s = "", *mask_s = "", *route_s = "", *dns1_s = "", *dns2_s = "";
    int status;

    if (!buffer || !size) return -1;
    status = sceNetCtlInit();
    /* NetCtl may already be initialized by the system. GetInfo is authoritative;
     * proceed even when Init reports that state. */
    (void)status;

    status = get_info(ORBIS_NET_CTL_INFO_IP_ADDRESS, &ip, &ip_s);
    if (status < 0 || !ip_s[0] || !strcmp(ip_s, "0.0.0.0")) return status < 0 ? status : -2;
    if (get_info(ORBIS_NET_CTL_INFO_NETMASK, &mask, &mask_s) < 0) mask_s = "";
    if (get_info(ORBIS_NET_CTL_INFO_DEFAULT_ROUTE, &route, &route_s) < 0) route_s = "";
    if (get_info(ORBIS_NET_CTL_INFO_PRIMARY_DNS, &dns1, &dns1_s) < 0) dns1_s = "";
    if (get_info(ORBIS_NET_CTL_INFO_SECONDARY_DNS, &dns2, &dns2_s) < 0) dns2_s = "";
    status = sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_MTU, &mtu);
    if (status < 0) mtu.mtu = 1500;

    if (snprintf(buffer, size, "%s,%s,%s,%s,%s,%u", ip_s, mask_s, route_s,
                 dns1_s, dns2_s, (unsigned)mtu.mtu) >= (int)size)
        return -3;
    return 0;
}
