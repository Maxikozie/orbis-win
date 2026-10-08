/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* libvulkan.prx's entry points on PS4.
 *
 * win32u reaches Vulkan by dlopen(SONAME_LIBVULKAN) -- "libvulkan.so" -- and
 * pw_wine_dl turns that into libvulkan.prx (pw_wine_dl.c:52). It then looks up
 * vkGetInstanceProcAddr and vkGetDeviceProcAddr. Mesa's archive exports
 * neither: it exports the ICD's door, vk_icdGetInstanceProcAddr, and the
 * common device dispatch, exactly as the bundle's vkloader documents. So this
 * file is the same two-symbol shim, which is all a loader on this console can
 * be -- there is no libvulkan.so to find and no icd.d to read.
 *
 * ⚠ This is the PS4 arm of wine/ps5/pw_vulkan_radv.c, not a copy of it. That
 * one also bridges presentation, forwarding pw_videoout_* to PS5_Mesa's
 * wsi_videoout_idle / _show_tiled / _present_rect. This Mesa has no such
 * symbols -- the PS4 build carries the stock Mesa WSI instead, 64 wsi_* entry
 * points including wsi_CreateSwapchainKHR -- so there is nothing to forward
 * to, and nothing imports pw_videoout_* here either (win32u.shared.elf names
 * none of them). Presentation is its own problem; this gets the driver
 * loadable, which is what every part of the graphics stack is waiting on. */
#include <vulkan/vulkan_core.h>

PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice device, const char *name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
    return vk_icdGetInstanceProcAddr(instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name)
{
    return vk_common_GetDeviceProcAddr(device, name);
}
