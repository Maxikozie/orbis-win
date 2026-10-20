/* Copyright © 2026 Mikołaj Mikołajczyk
 * SPDX-License-Identifier: MIT
 *
 * A triangle on the television, from the bundle, in one file.
 *
 * WHAT THIS IS FOR. hello/ proves the bundle's LAYOUT: corrected headers, the overlay archive under
 * --whole-archive, the linker script, crt1.o, and create-fself turning the result into an eboot. It
 * deliberately touches no Mesa. This one is the other half - the workflow a person actually came
 * here for. It answers, in one run and with no engine, whether RADV comes up on this console and
 * whether a frame reaches the screen.
 *
 * ⚠ THE SURFACE IS HEADLESS, AND THAT IS THE ONE PLATFORM-SPECIFIC LINE IN THE WHOLE FILE. There is
 * no window system here, so there is no VkSurfaceKHR to attach to one. The port creates a
 * VK_EXT_headless_surface and mesa-ps4's wsi_orbis.c turns its presents into
 * sceVideoOutRegisterBuffers + sceVideoOutSubmitFlip. Tempest does exactly this and says why:
 *
 *     ⚠ hwnd IS IGNORED (...) a headless surface has nothing to attach to anyway: it names THE
 *     display, of which there is one.                    - Tempest vswapchain.cpp:264
 *
 * Everything else below is ordinary Vulkan and would compile for any platform.
 *
 * ⚠ AND IT DOES NOT RETURN FROM main(). Returning is reported as CE-34878-0 on this console and
 * reads exactly like a crash - see README section 0. It draws, presents, reports, and idles. */

#include <orbis_prefix.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <vulkan/vulkan.h>
#include <orbis/libkernel.h>

#include "hang_vert_spv.h"
#include "hang_frag_spv.h"

#if defined(GFXHANG_SINGLE_SHOT) || defined(GFXHANG_REPEAT_ONE_DRAW)
enum { DRAWS_PER_FRAME = 1 };
#else
enum { DRAWS_PER_FRAME = 64 };
#endif
#ifdef GFXHANG_REPEAT_ONE_DRAW
enum { RUN_DURATION_SECONDS = 60 };
#else
enum { RUN_DURATION_SECONDS = 360 };
#endif

/* One channel, used by everything below. sceKernelDebugOutText is the console's own and needs no
 * library of ours, which matters when the thing being tested is the graphics stack. */
static void say(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    sceKernelDebugOutText(0, buf);
    fputs(buf, stdout);
    fflush(stdout);
}

/* ⚠ EVERY CALL IS CHECKED AND NAMED. A Vulkan example that ignores results is a Vulkan example that
 * reports a black screen and nothing else - and a black screen is what this console shows for a
 * dozen different reasons. The step that failed is the whole value of running this. */
static void must(VkResult r, const char *what)
{
    if (r != VK_SUCCESS) {
        say("gfxhang: FAILED at %s -> VkResult %d", what, (int)r);
        say("gfxhang: idling so the log can be read; close from the PS button menu");
        for (;;)
            sleep(1);
    }
    say("triangle: ok   %s", what);
}

int main(void)
{
#ifdef GFXHANG_FORCE_WD_SWITCH_ON_EOP
    if (setenv("ORBIS_FORCE_WD_SWITCH_ON_EOP", "1", 1) != 0) {
        say("gfxhang: FAILED to set WD_SWITCH_ON_EOP experiment environment");
        for (;;) sleep(1);
    }
    say("gfxhang: forcing RADV WD_SWITCH_ON_EOP on Neo");
#endif
#ifdef GFXHANG_DISABLE_IA_SWITCH_ON_EOI
    if (setenv("ORBIS_DISABLE_IA_SWITCH_ON_EOI", "1", 1) != 0) {
        say("gfxhang: FAILED to set IA_SWITCH_ON_EOI experiment environment");
        for (;;) sleep(1);
    }
    say("gfxhang: disabling RADV IA_SWITCH_ON_EOI on Neo");
#endif
#ifdef GFXHANG_SINGLE_SHOT
    say("gfxhang: single indexed draw + sampled image, one frame only");
#elif defined(GFXHANG_REPEAT_ONE_DRAW)
#if defined(GFXHANG_SOLID_FRAGMENT) && defined(GFXHANG_UNINDEXED_DRAW)
    say("gfxhang: single nonindexed vertex-buffer draw + solid fragment, repeated frames");
#elif defined(GFXHANG_SOLID_FRAGMENT) && defined(GFXHANG_LOW_INDEX_VALUES)
    say("gfxhang: indexed draw count=%u, low indices, solid fragment, repeated frames",
        (unsigned)GFXHANG_DRAW_INDEX_COUNT);
#elif defined(GFXHANG_SOLID_FRAGMENT)
    say("gfxhang: indexed draw count=%u, solid fragment, repeated frames",
        (unsigned)GFXHANG_DRAW_INDEX_COUNT);
#else
    say("gfxhang: single indexed draw + sampled image, repeated frames");
#endif
#else
    say("gfxhang: indexed draw + sampled image stress, 6 minute run");
#endif

    /* ---------------------------------------------------------------- instance */
    const char *inst_ext[] = { "VK_KHR_surface", "VK_EXT_headless_surface" };
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "gladius-gfxhang",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = inst_ext,
    };
    VkInstance instance;
    must(vkCreateInstance(&ici, NULL, &instance), "vkCreateInstance");

    /* ---------------------------------------------------------------- the display */
    VkHeadlessSurfaceCreateInfoEXT hsci = { .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT };
    VkSurfaceKHR surface;
    must(vkCreateHeadlessSurfaceEXT(instance, &hsci, NULL, &surface), "vkCreateHeadlessSurfaceEXT");

    /* ---------------------------------------------------------------- device */
    uint32_t ndev = 0;
    must(vkEnumeratePhysicalDevices(instance, &ndev, NULL), "vkEnumeratePhysicalDevices(count)");
    if (ndev == 0) {
        say("gfxhang: FAILED - no physical device. RADV did not enumerate this GPU.");
        for (;;) sleep(1);
    }
    VkPhysicalDevice phys[4];
    if (ndev > 4) ndev = 4;
    must(vkEnumeratePhysicalDevices(instance, &ndev, phys), "vkEnumeratePhysicalDevices");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys[0], &props);
    say("gfxhang: GPU '%s', api %u.%u.%u, driver 0x%08x",
        props.deviceName,
        VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion), props.driverVersion);

    /* The one queue family that can both render and present. On this driver they are the same
     * family, but asking is two lines and assuming is a bug on the next GPU. */
    uint32_t nqf = 0, qf = UINT32_MAX;
    vkGetPhysicalDeviceQueueFamilyProperties(phys[0], &nqf, NULL);
    VkQueueFamilyProperties *qfp = calloc(nqf, sizeof(*qfp));
    vkGetPhysicalDeviceQueueFamilyProperties(phys[0], &nqf, qfp);
    for (uint32_t i = 0; i < nqf; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys[0], i, surface, &present);
        if ((qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { qf = i; break; }
    }
    free(qfp);
    if (qf == UINT32_MAX) {
        say("gfxhang: FAILED - no queue family that both renders and presents");
        for (;;) sleep(1);
    }
    say("gfxhang: queue family %u renders and presents", qf);

    const float prio = 1.0f;
    const char *dev_ext[] = { "VK_KHR_swapchain" };
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = dev_ext,
    };
    VkDevice device;
    must(vkCreateDevice(phys[0], &dci, NULL, &device), "vkCreateDevice");

    VkQueue queue;
    vkGetDeviceQueue(device, qf, 0, &queue);

    /* Small real sampled image: clear it through the transfer path below, then
     * bind it through an ordinary combined-image-sampler descriptor. */
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { 4, 4, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage sampled_image;
    must(vkCreateImage(device, &image_info, NULL, &sampled_image), "vkCreateImage(sampled)");
    VkMemoryRequirements image_req;
    vkGetImageMemoryRequirements(device, sampled_image, &image_req);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys[0], &mem_props);
    uint32_t image_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        if ((image_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            image_type = i; break;
        }
    if (image_type == UINT32_MAX) {
        say("gfxhang: FAILED - no device-local memory type for sampled image");
        for (;;) sleep(1);
    }
    VkMemoryAllocateInfo image_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_req.size, .memoryTypeIndex = image_type,
    };
    VkDeviceMemory image_memory;
    must(vkAllocateMemory(device, &image_alloc, NULL, &image_memory), "vkAllocateMemory(sampled)");
    must(vkBindImageMemory(device, sampled_image, image_memory, 0), "vkBindImageMemory(sampled)");
    VkImageViewCreateInfo sampled_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = sampled_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkImageView sampled_view;
    must(vkCreateImageView(device, &sampled_view_info, NULL, &sampled_view), "vkCreateImageView(sampled)");
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT, .maxLod = 0.0f,
    };
    VkSampler sampler;
    must(vkCreateSampler(device, &sampler_info, NULL, &sampler), "vkCreateSampler");

    /* ---------------------------------------------------------------- swapchain */
    VkSurfaceCapabilitiesKHR caps;
    must(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys[0], surface, &caps),
         "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t nfmt = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys[0], surface, &nfmt, NULL);
    VkSurfaceFormatKHR *fmts = calloc(nfmt, sizeof(*fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys[0], surface, &nfmt, fmts);
    const VkSurfaceFormatKHR fmt = fmts[0];
    free(fmts);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) { extent.width = 1920; extent.height = 1080; }
    say("gfxhang: surface %ux%u, format %d, %u..%u images",
        extent.width, extent.height, (int)fmt.format, caps.minImageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount,
        .imageFormat = fmt.format, .imageColorSpace = fmt.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,   /* always supported, and vsync is what a TV wants */
        .clipped = VK_TRUE,
    };
    VkSwapchainKHR swapchain;
    must(vkCreateSwapchainKHR(device, &sci, NULL, &swapchain), "vkCreateSwapchainKHR");

    uint32_t nimg = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &nimg, NULL);
    VkImage *images = calloc(nimg, sizeof(*images));
    vkGetSwapchainImagesKHR(device, swapchain, &nimg, images);
    say("gfxhang: swapchain has %u images", nimg);

    /* ---------------------------------------------------------------- render pass */
    VkAttachmentDescription att = {
        .format = fmt.format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference ref = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &ref,
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att,
        .subpassCount = 1, .pSubpasses = &sub,
    };
    VkRenderPass rp;
    must(vkCreateRenderPass(device, &rpci, NULL, &rp), "vkCreateRenderPass");

    /* ---------------------------------------------------------------- views, framebuffers */
    VkImageView   *views = calloc(nimg, sizeof(*views));
    VkFramebuffer *fbs   = calloc(nimg, sizeof(*fbs));
    for (uint32_t i = 0; i < nimg; i++) {
        VkImageViewCreateInfo ivci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        must(vkCreateImageView(device, &ivci, NULL, &views[i]), "vkCreateImageView");
        VkFramebufferCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = rp, .attachmentCount = 1, .pAttachments = &views[i],
            .width = extent.width, .height = extent.height, .layers = 1,
        };
        must(vkCreateFramebuffer(device, &fci, NULL, &fbs[i]), "vkCreateFramebuffer");
    }

    /* ---------------------------------------------------------------- pipeline */
    VkShaderModuleCreateInfo vsci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(hang_vert_spv), .pCode = hang_vert_spv,
    };
    VkShaderModuleCreateInfo fsci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(hang_frag_spv), .pCode = hang_frag_spv,
    };
    VkShaderModule vs, fs;
    must(vkCreateShaderModule(device, &vsci, NULL, &vs), "vkCreateShaderModule(vert)");
    must(vkCreateShaderModule(device, &fsci, NULL, &fs), "vkCreateShaderModule(frag)");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" },
    };
    VkVertexInputBindingDescription vertex_binding = {
        .binding = 0, .stride = 36, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription vertex_attribute = {
        .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0,
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 1, .pVertexAttributeDescriptions = &vertex_attribute,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
#ifdef GFXHANG_POINT_LIST
        .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
#else
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
#endif
    };
    VkViewport vp = { 0, 0, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D   sc = { { 0, 0 }, extent };
    VkPipelineViewportStateCreateInfo vps = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba,
    };
    VkDescriptorSetLayoutBinding sampled_binding = {
        .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &sampled_binding,
    };
    VkDescriptorSetLayout descriptor_layout;
    must(vkCreateDescriptorSetLayout(device, &dslci, NULL, &descriptor_layout), "vkCreateDescriptorSetLayout");
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &descriptor_layout,
    };
    VkPipelineLayout layout;
    must(vkCreatePipelineLayout(device, &plci, NULL, &layout), "vkCreatePipelineLayout");

    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size,
    };
    VkDescriptorPool descriptor_pool;
    must(vkCreateDescriptorPool(device, &dpci, NULL, &descriptor_pool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool, .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_layout,
    };
    VkDescriptorSet descriptor_set;
    must(vkAllocateDescriptorSets(device, &dsai, &descriptor_set), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo descriptor_image = {
        .sampler = sampler, .imageView = sampled_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet descriptor_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set, .dstBinding = 0,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptor_image,
    };
    vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);

    VkGraphicsPipelineCreateInfo gpci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi, .pInputAssemblyState = &ia, .pViewportState = &vps,
        .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &cb,
        .layout = layout, .renderPass = rp, .subpass = 0,
    };
    VkPipeline pipeline;
    /* ⚠ THE FIRST PIPELINE COMPILE IS THE ONE THAT NEEDS A STACK. README section 2.8: a dEQP worker died
     * inside radv_graphics_shaders_compile with about 72 KB of frame under it, and every thread that
     * compiles a pipeline on this console has the same cliff. This happens on the main thread here,
     * which has 2 MB, so it is not a test of the interposer - it is the call that would have found it. */
    must(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline),
         "vkCreateGraphicsPipelines");

    /* ---------------------------------------------------------------- commands */
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qf,
    };
    VkCommandPool pool;
    must(vkCreateCommandPool(device, &cpci, NULL, &pool), "vkCreateCommandPool");

    /* Initialize the sampled image to a known nonzero texel and transition it
     * to the shader-read layout before any draw can access its descriptor. */
    VkCommandBuffer init_cmd;
    VkCommandBufferAllocateInfo init_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    must(vkAllocateCommandBuffers(device, &init_alloc, &init_cmd), "vkAllocateCommandBuffers(init)");
    VkCommandBufferBeginInfo init_begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    must(vkBeginCommandBuffer(init_cmd, &init_begin), "vkBeginCommandBuffer(init)");
    VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sampled_image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(init_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &image_barrier);
    VkClearColorValue texel = { .float32 = { 0.25f, 0.75f, 0.5f, 1.0f } };
    VkImageSubresourceRange texel_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(init_cmd, sampled_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &texel, 1, &texel_range);
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(init_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &image_barrier);
    must(vkEndCommandBuffer(init_cmd), "vkEndCommandBuffer(init)");
    VkSubmitInfo init_submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &init_cmd };
    must(vkQueueSubmit(queue, 1, &init_submit, VK_NULL_HANDLE), "vkQueueSubmit(init)");
    must(vkQueueWaitIdle(queue), "vkQueueWaitIdle(init)");

    VkBufferCreateInfo index_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 4096 * sizeof(uint16_t),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer index_buffer;
    must(vkCreateBuffer(device, &index_info, NULL, &index_buffer), "vkCreateBuffer(index)");
    VkMemoryRequirements index_req;
    vkGetBufferMemoryRequirements(device, index_buffer, &index_req);
    uint32_t index_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        if ((index_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            index_type = i; break;
        }
    if (index_type == UINT32_MAX) {
        say("gfxhang: FAILED - no host-visible coherent memory type for indices");
        for (;;) sleep(1);
    }
    VkMemoryAllocateInfo index_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = index_req.size, .memoryTypeIndex = index_type,
    };
    VkDeviceMemory index_memory;
    must(vkAllocateMemory(device, &index_alloc, NULL, &index_memory), "vkAllocateMemory(index)");
    must(vkBindBufferMemory(device, index_buffer, index_memory, 0), "vkBindBufferMemory(index)");
    uint16_t *indices = NULL;
    must(vkMapMemory(device, index_memory, 0, index_req.size, 0, (void **)&indices), "vkMapMemory(index)");
    for (uint32_t i = 0; i < 4096; ++i) {
#ifdef GFXHANG_LOW_INDEX_VALUES
        indices[i] = (uint16_t)(i % 3);
#else
        indices[i] = (uint16_t)(i % 910);
#endif
    }
    vkUnmapMemory(device, index_memory);
    VkBufferCreateInfo vertex_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 910 * 36,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer vertex_buffer;
    must(vkCreateBuffer(device, &vertex_info, NULL, &vertex_buffer), "vkCreateBuffer(vertex)");
    VkMemoryRequirements vertex_req;
    vkGetBufferMemoryRequirements(device, vertex_buffer, &vertex_req);
    uint32_t vertex_type = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
        if ((vertex_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vertex_type = i; break;
        }
    if (vertex_type == UINT32_MAX) {
        say("gfxhang: FAILED - no host-visible coherent memory type for vertices");
        for (;;) sleep(1);
    }
    VkMemoryAllocateInfo vertex_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = vertex_req.size, .memoryTypeIndex = vertex_type,
    };
    VkDeviceMemory vertex_memory;
    must(vkAllocateMemory(device, &vertex_alloc, NULL, &vertex_memory), "vkAllocateMemory(vertex)");
    must(vkBindBufferMemory(device, vertex_buffer, vertex_memory, 0), "vkBindBufferMemory(vertex)");
    float (*vertices)[9] = NULL;
    must(vkMapMemory(device, vertex_memory, 0, vertex_req.size, 0, (void **)&vertices), "vkMapMemory(vertex)");
    const float positions[3][3] = {
        { 0.0f, -0.8f, 0.0f }, { 0.8f, 0.8f, 0.0f }, { -0.8f, 0.8f, 0.0f },
    };
    for (uint32_t i = 0; i < 910; ++i) {
        memcpy(vertices[i], positions[i % 3], sizeof(positions[0]));
        for (unsigned j = 3; j < 9; ++j) vertices[i][j] = 0.0f;
    }
    vkUnmapMemory(device, vertex_memory);
#ifdef GFXHANG_LOW_INDEX_VALUES
    say("gfxhang: draw data ready: draw-index-count=%u max-index=2; 910 vertices, stride=36",
        (unsigned)GFXHANG_DRAW_INDEX_COUNT);
#else
    say("gfxhang: draw data ready: draw-index-count=%u max-index=%u; 910 vertices, stride=36",
        (unsigned)GFXHANG_DRAW_INDEX_COUNT,
        ((unsigned)GFXHANG_DRAW_INDEX_COUNT - 1u) % 910u);
#endif

    VkCommandBuffer *cmds = calloc(nimg, sizeof(*cmds));
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = nimg,
    };
    must(vkAllocateCommandBuffers(device, &cbai, cmds), "vkAllocateCommandBuffers");

    for (uint32_t i = 0; i < nimg; i++) {
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmds[i], &bi);
        VkClearValue clear = { .color = { .float32 = { 0.05f, 0.05f, 0.12f, 1.0f } } };
        VkRenderPassBeginInfo rpbi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = rp, .framebuffer = fbs[i],
            .renderArea = { { 0, 0 }, extent },
            .clearValueCount = 1, .pClearValues = &clear,
        };
        vkCmdBeginRenderPass(cmds[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmds[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmds[i], VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                0, 1, &descriptor_set, 0, NULL);
#ifndef GFXHANG_UNINDEXED_DRAW
        vkCmdBindIndexBuffer(cmds[i], index_buffer, 0, VK_INDEX_TYPE_UINT16);
#endif
        VkDeviceSize vertex_offset = 0;
        vkCmdBindVertexBuffers(cmds[i], 0, 1, &vertex_buffer, &vertex_offset);
#ifdef GFXHANG_UNINDEXED_DRAW
        for (unsigned draw = 0; draw < DRAWS_PER_FRAME; ++draw)
            vkCmdDraw(cmds[i], 3, 1, 0, 0);
#else
        for (unsigned draw = 0; draw < DRAWS_PER_FRAME; ++draw)
            vkCmdDrawIndexed(cmds[i], GFXHANG_DRAW_INDEX_COUNT, 1, 0, 0, 0);
#endif
        vkCmdEndRenderPass(cmds[i]);
        vkEndCommandBuffer(cmds[i]);
    }

    VkSemaphoreCreateInfo semci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore acquired, rendered;
    must(vkCreateSemaphore(device, &semci, NULL, &acquired), "vkCreateSemaphore(acquired)");
    must(vkCreateSemaphore(device, &semci, NULL, &rendered), "vkCreateSemaphore(rendered)");

    /* ---------------------------------------------------------------- present for six minutes */
#ifdef GFXHANG_SINGLE_SHOT
    say("gfxhang: setup complete - submitting one frame");
#elif defined(GFXHANG_REPEAT_ONE_DRAW)
    say("gfxhang: setup complete - beginning 60-second run");
#else
#ifdef GFXHANG_SINGLE_SHOT
    say("gfxhang: setup complete - submitting one frame");
#else
#ifdef GFXHANG_SINGLE_SHOT
    say("gfxhang: setup complete - submitting one frame");
#else
    say("gfxhang: setup complete - beginning 360-second run");
#endif
#endif
#endif
    unsigned long frame = 0;
    int runtime_error = 0;
    time_t started = time(NULL), last_report = started;
    while (time(NULL) - started < RUN_DURATION_SECONDS) {
        uint32_t idx = 0;
        VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &idx);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            say("gfxhang: vkAcquireNextImageKHR -> %d at frame %lu", (int)r, frame);
            runtime_error = 1;
            break;
        }
        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1, .pWaitSemaphores = &acquired, .pWaitDstStageMask = &wait,
            .commandBufferCount = 1, .pCommandBuffers = &cmds[idx],
            .signalSemaphoreCount = 1, .pSignalSemaphores = &rendered,
        };
        r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        if (r != VK_SUCCESS) { say("gfxhang: vkQueueSubmit -> %d at frame %lu", (int)r, frame); runtime_error = 1; break; }

        VkPresentInfoKHR pi = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1, .pWaitSemaphores = &rendered,
            .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &idx,
        };
        r = vkQueuePresentKHR(queue, &pi);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            say("gfxhang: vkQueuePresentKHR -> %d at frame %lu", (int)r, frame);
            runtime_error = 1;
            break;
        }
        r = vkQueueWaitIdle(queue);
        if (r != VK_SUCCESS) {
            say("gfxhang: vkQueueWaitIdle -> %d at frame %lu", (int)r, frame);
            runtime_error = 1;
            break;
        }

        /* Powers of two, for the same reason README section 2.8's census uses them: a line per frame
         * would put this port's own log channel in the state it warns about. */
        time_t now = time(NULL);
        if (now - last_report >= 10) {
            say("gfxhang: elapsed=%ld s frames=%lu draw-calls=%lu last-frame-complete",
                (long)(now - started), frame, frame * DRAWS_PER_FRAME);
            last_report = now;
        }
        frame++;
#ifdef GFXHANG_SINGLE_SHOT
        break;
#endif
    }

#ifdef GFXHANG_SINGLE_SHOT
    if (!runtime_error && frame == 1)
        say("gfxhang: PASS single-shot frame=1 indexed-draws=1; idling for logs");
    else
        say("gfxhang: STOPPED before single-shot completion frames=%lu error=%d", frame, runtime_error);
#else
    if (!runtime_error && time(NULL) - started >= RUN_DURATION_SECONDS)
        say("gfxhang: PASS duration=%ds frames=%lu draw-calls=%lu - close from the PS button menu",
            RUN_DURATION_SECONDS,
            frame, frame * DRAWS_PER_FRAME);
    else
        say("gfxhang: STOPPED duration=%ld s frames=%lu error=%d - inspect preceding line",
            (long)(time(NULL) - started), frame, runtime_error);
#endif
    for (;;)
        sleep(1);
}
