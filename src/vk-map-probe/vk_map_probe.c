/* End-to-end placed CPU mapping: GPU copy, reserve, remap, GPU copy again.
 * Reservations use NO_OVERWRITE. Failed runs retain resources until title exit.
 */
#include <vulkan/vulkan.h>
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

static void note(const char *fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line) - 2, fmt, args);
    va_end(args);
    size_t n = 0;
    while (line[n]) ++n;
    line[n++] = '\n'; line[n] = 0;
    sceKernelDebugOutText(0, line);
}
#define CHECK(call) do { VkResult rc = (call); if (rc != VK_SUCCESS) { \
    note("vkmapprobe: FAIL %s rc=%d", #call, rc); return 1; } } while (0)

static int trial(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                 uint32_t family, uint32_t type, VkDeviceSize payload)
{
    VkBuffer buffers[2];
    VkDeviceMemory memories[2];
    void *views[2] = {0};
    VkDeviceSize sizes[2];
    PFN_vkMapMemory2KHR map = (PFN_vkMapMemory2KHR)vkGetDeviceProcAddr(device, "vkMapMemory2KHR");
    PFN_vkUnmapMemory2KHR unmap = (PFN_vkUnmapMemory2KHR)vkGetDeviceProcAddr(device, "vkUnmapMemory2KHR");
    if (!map || !unmap) { note("vkmapprobe: FAIL map/unmap entry points"); return 1; }
    for (unsigned i = 0; i < 2; ++i) {
        VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = payload, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        CHECK(vkCreateBuffer(device, &info, NULL, &buffers[i]));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, buffers[i], &req);
        if (!(req.memoryTypeBits & (1u << type))) {
            note("vkmapprobe: SKIP memory type %u incompatible with transfer buffer", type);
            vkDestroyBuffer(device, buffers[i], NULL);
            return 2;
        }
        sizes[i] = req.size; /* Exercise non-kernel-page allocation sizes too. */
        VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = sizes[i], .memoryTypeIndex = type};
        CHECK(vkAllocateMemory(device, &alloc, NULL, &memories[i]));
        CHECK(vkBindBufferMemory(device, buffers[i], memories[i], 0));
        for (uintptr_t candidate = 0x10000000 + i * 0x1000000;
             candidate < 0x70000000; candidate += 0x2000000) {
            void *at = (void *)candidate;
            int rc = sceKernelReserveVirtualRange(&at, (sizes[i] + 16383) & ~(VkDeviceSize)16383, 0x10 | 0x80, 16384);
            if (!rc && at == (void *)candidate) { views[i] = at; break; }
            if (!rc) sceKernelMunmap(at, (sizes[i] + 16383) & ~(VkDeviceSize)16383);
        }
        if (!views[i]) { note("vkmapprobe: FAIL low reservation"); return 1; }
    }
    VkCommandPool pool;
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkFence fence;
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < 2; ++i) {
            VkMemoryMapPlacedInfoEXT placed = {.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT,
                .pPlacedAddress = views[i]};
            VkMemoryMapInfoKHR info = {.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR,
                .pNext = &placed, .flags = VK_MEMORY_MAP_PLACED_BIT_EXT,
                .memory = memories[i], .size = VK_WHOLE_SIZE};
            void *mapped = NULL;
            CHECK(map(device, &info, &mapped));
            note("vkmapprobe: type=%u pass=%u placed=%p got=%p bytes=%llu",
                 type, pass, views[i], mapped, (unsigned long long)sizes[i]);
            if (mapped != views[i] || (uintptr_t)mapped + sizes[i] > UINT64_C(0x100000000))
                return 1;
        }
        volatile uint32_t *src = views[0], *dst = views[1];
        for (unsigned i = 0; i < payload / 4; ++i) { src[i] = 0x12345678u ^ i ^ pass; dst[i] = 0; }
        __sync_synchronize();
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkBeginCommandBuffer(command, &begin));
        VkBufferCopy copy = {.size = payload};
        vkCmdCopyBuffer(command, buffers[0], buffers[1], 1, &copy);
        VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &barrier, 0, NULL, 0, NULL);
        CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1, .pCommandBuffers = &command};
        CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
        __sync_synchronize();
        unsigned bad = 0;
        for (unsigned i = 0; i < payload / 4; ++i) bad += dst[i] != (0x12345678u ^ i ^ pass);
        note("vkmapprobe: type=%u pass=%u GPU COPY words=%llu mismatches=%u",
             type, pass, (unsigned long long)(payload / 4), bad);
        if (bad) return 1;
        for (unsigned i = 0; i < 2; ++i) {
            VkMemoryUnmapInfoKHR info = {.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR,
                .flags = pass == 0 ? VK_MEMORY_UNMAP_RESERVE_BIT_EXT : 0, .memory = memories[i]};
            CHECK(unmap(device, &info));
        }
        CHECK(vkResetFences(device, 1, &fence));
        CHECK(vkResetCommandBuffer(command, 0));
    }
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    for (unsigned i = 0; i < 2; ++i) {
        vkDestroyBuffer(device, buffers[i], NULL);
        vkFreeMemory(device, memories[i], NULL);
    }
    (void)physical;
    note("vkmapprobe: type=%u PASS including reserve/remap and cleanup", type);
    return 0;
}

static int run(void)
{
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Gladius Vulkan Mapping Probe", .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t count = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    if (!count) return 1;
    VkQueueFamilyProperties families[16]; count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == count) return 1;
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceMapMemoryPlacedFeaturesEXT placed = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT,
        .memoryMapPlaced = VK_TRUE, .memoryUnmapReserve = VK_TRUE};
    const char *extensions[] = {VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME, VK_KHR_MAP_MEMORY_2_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &placed, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    unsigned tested = 0;
    for (uint32_t type = 0; type < memory.memoryTypeCount; ++type) {
        VkMemoryPropertyFlags flags = memory.memoryTypes[type].propertyFlags;
        if ((flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) !=
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) continue;
        note("vkmapprobe: testing type=%u flags=%#x", type, flags);
        int result = trial(physical, device, queue, family, type, 65536);
        if (result == 2) continue;
        if (result) return 1;
        if (trial(physical, device, queue, family, type, 4096)) return 1;
        ++tested;
    }
    CHECK(vkDeviceWaitIdle(device));
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    note("vkmapprobe: DONE tested=%u result=%s", tested, tested ? "PASS" : "FAIL");
    return !tested;
}
int main(void)
{
    note("vkmapprobe: START");
    int rc = run();
    if (rc) note("vkmapprobe: DONE FAIL; resources retained until title exit");
    for (;;) sleep(1);
}
