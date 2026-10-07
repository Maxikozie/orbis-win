/* Gladius probe — the stage 2 instrument.
 *
 * Reports what the driver thinks it is sitting on, through the Vulkan API
 * alone. No swapchain, no device, no rendering: instance, enumerate, query,
 * print. Everything that can fail here is something we want to know about.
 *
 * ⚠ WHY THIS EXISTS RATHER THAN RADV_DEBUG=info.
 *
 * The upstream record captures radeon_info with RADV_DEBUG=info, and that dump
 * is the comparison the whole port rests on. Two things stop us doing the same
 * from a stock bundle:
 *
 *   1. The dump does not go through mesa_log, and on this platform it cannot go
 *      through stdout either - freopen fails. Upstream's harness patches the one
 *      call site to fopen a file directly. We have Mesa as a prebuilt archive,
 *      so we cannot patch it without rebuilding Mesa.
 *   2. RADV_DEBUG has to be SET, and on this console it cannot be - see the
 *      setenv note below.
 *
 * VK_AMD_shader_core_properties carries the fields that actually decide stage 3
 * (shaderEngineCount, shaderArraysPerEngineCount, computeUnitsPerShaderArray =
 * max_se, max_sa_per_se, num_cu_per_sh), and it is present in this bundle's
 * RADV. deviceID answers whether the Gladius PCI branch fired. Between them
 * that is most of what the radeon_info diff was going to tell us, obtained
 * through a standard extension with no Mesa rebuild.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vulkan/vulkan.h>
#include <orbis/libkernel.h>

/* sceKernelDebugOutText is the console's own channel and needs no library of
 * ours, which matters when the thing under test is the graphics stack. */
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

/* ⚠ THE SWITCH THAT CANNOT BE THROWN.
 *
 * orbis-compat reads /data/orbis-env.txt into a PRIVATE table and answers from
 * it through orbis_env_get(). It does NOT setenv(). Mesa reads its knobs with
 * plain getenv() - RADV_DEBUG, and the port's own ORBIS_TESS_WORKGROUPS and
 * ORBIS_TF_BASE_BIAS - so on a stock bundle none of them can be set at all:
 * getenv returns NULL, the code takes its default, and the run comes back
 * looking like a measurement of the default.
 *
 * It works here because RADV is statically linked into THIS eboot, so Mesa and
 * this file share one environ. (That is also why it failed upstream for
 * RetroArch: a .prx core links its own copy of musl with its own environ, and
 * the frontend's setenv never reached it.)
 *
 * Same grammar as orbis-compat's reader: KEY=VALUE, '#' comments, whitespace
 * trimmed both sides of the '=' - a trailing space would otherwise read as a
 * different experiment.
 */
static int apply_env_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    int  n = 0;
    while (fgets(line, sizeof line, f)) {
        char *key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (*key == '#' || *key == '\n' || *key == '\r' || *key == '\0') continue;

        char *eq = strchr(key, '=');
        if (!eq) continue;
        *eq = '\0';

        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        for (char *e = eq - 1; e >= key && (*e == ' ' || *e == '\t'); e--) *e = '\0';
        for (char *e = val + strlen(val) - 1;
             e >= val && (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r'); e--) *e = '\0';

        if (*key) { setenv(key, val, 1); say("probe: env %s=%s", key, val); n++; }
    }
    fclose(f);
    return n;
}

static const char *heap_kind(VkMemoryHeapFlags f)
{
    return (f & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "device-local" : "host";
}

int main(void)
{
    say("=== gladius probe ===");

    int applied = apply_env_file("/data/orbis-env.txt");
    if (applied < 0)
        say("probe: no /data/orbis-env.txt (fine - nothing to override)");
    else
        say("probe: applied %d setting(s) from /data/orbis-env.txt", applied);

    /* ⚠ NEO MODE IS THE WHOLE QUESTION ON A PRO.
     *
     * A PS4 Pro runs a title as a base PS4 unless the title declares NEO
     * support — param.sfo ATTRIBUTE bit 24, 0x00800000. In base mode the extra
     * compute units are not there to be found: the GPU answers device id 0x9920
     * (CHIP_LIVERPOOL) and reports 2 SE x 1 SA x 9 CU = 18, which is exactly
     * what this probe saw on its first run against a Pro.
     *
     *   HasNeoMode  the CONSOLE is a Pro
     *   IsNeoMode   THIS TITLE got Neo mode
     *
     * Both matter: 1/0 means the package is the problem, 0/0 means something
     * else entirely. */
    say("probe: sceKernelHasNeoMode()=%d  sceKernelIsNeoMode()=%d",
        (int)sceKernelHasNeoMode(), (int)sceKernelIsNeoMode());

    VkApplicationInfo app = {
        .sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "gladius-probe",
        .apiVersion = VK_API_VERSION_1_1,   /* 1.1 for vkGetPhysicalDeviceProperties2 */
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };

    VkInstance instance;
    VkResult   r = vkCreateInstance(&ici, NULL, &instance);
    if (r != VK_SUCCESS) {
        say("probe: FAILED vkCreateInstance -> %d", (int)r);
        say("probe: idling so the log can be read; close from the PS button menu");
        for (;;) sleep(1);
    }
    say("probe: ok   vkCreateInstance");

    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(instance, &ndev, NULL);
    if (ndev == 0) {
        say("probe: FAILED - no physical device. RADV did not enumerate this GPU.");
        say("probe: idling so the log can be read; close from the PS button menu");
        for (;;) sleep(1);
    }
    VkPhysicalDevice phys[4];
    if (ndev > 4) ndev = 4;
    vkEnumeratePhysicalDevices(instance, &ndev, phys);
    say("probe: %u physical device(s)", ndev);

    /* Is the AMD topology extension there? It is a device extension, but its
     * properties come back from a physical-device query. Report the answer
     * either way rather than silently printing zeroes. */
    uint32_t next = 0;
    vkEnumerateDeviceExtensionProperties(phys[0], NULL, &next, NULL);
    VkExtensionProperties *ext = calloc(next ? next : 1, sizeof *ext);
    vkEnumerateDeviceExtensionProperties(phys[0], NULL, &next, ext);
    int have_core_props = 0;
    for (uint32_t i = 0; i < next; i++)
        if (strcmp(ext[i].extensionName, VK_AMD_SHADER_CORE_PROPERTIES_EXTENSION_NAME) == 0)
            have_core_props = 1;
    say("probe: %u device extensions, VK_AMD_shader_core_properties %s",
        next, have_core_props ? "present" : "ABSENT");
    free(ext);

    VkPhysicalDeviceShaderCorePropertiesAMD core = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD,
    };
    VkPhysicalDeviceProperties2 p2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = have_core_props ? &core : NULL,
    };
    vkGetPhysicalDeviceProperties2(phys[0], &p2);
    VkPhysicalDeviceProperties *p = &p2.properties;

    say("");
    say("--- identity -------------------------------------------------");
    /* deviceName is RADV's marketing_name. This one line is the stage 2 gate:
     * Liverpool means the Gladius branch did not fire. */
    say("deviceName        %s", p->deviceName);
    /* 0x9924 is CHIP_GLADIUS in ac_gpu_info.c; the base PS4 is a different id. */
    say("vendorID          0x%04x", p->vendorID);
    say("deviceID          0x%04x%s", p->deviceID,
        p->deviceID == 0x9924 ? "   <- CHIP_GLADIUS" : "");
    say("apiVersion        %u.%u.%u", VK_VERSION_MAJOR(p->apiVersion),
        VK_VERSION_MINOR(p->apiVersion), VK_VERSION_PATCH(p->apiVersion));
    say("driverVersion     0x%08x", p->driverVersion);

    if (have_core_props) {
        say("");
        say("--- shader core topology  (vs Liverpool: 2 SE, 1 SA, 9 CU/SA = 18) ---");
        say("shaderEngineCount        %u      <- max_se", core.shaderEngineCount);
        say("shaderArraysPerEngine    %u      <- max_sa_per_se", core.shaderArraysPerEngineCount);
        say("computeUnitsPerShaderArr %u      <- num_cu_per_sh", core.computeUnitsPerShaderArray);
        say("=> compute units         %u",
            core.shaderEngineCount * core.shaderArraysPerEngineCount *
            core.computeUnitsPerShaderArray);
        say("simdPerComputeUnit       %u", core.simdPerComputeUnit);
        say("wavefrontsPerSimd        %u", core.wavefrontsPerSimd);
        say("wavefrontSize            %u", core.wavefrontSize);
        /* The number stage 3's tess ring sizing is derived from. Upstream pins
         * it to 256 on Liverpool because topology predicts 72 and the hardware
         * overruns the ring that sizes. On a 4-SE part it predicts something
         * else, which is the point of printing it here. */
        say("");
        say("tess workgroup prediction (4/CU capped per SE, x max_se): %u",
            4 * core.computeUnitsPerShaderArray * core.shaderArraysPerEngineCount *
            core.shaderEngineCount);
    }

    say("");
    say("--- memory ---------------------------------------------------");
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(phys[0], &mem);
    for (uint32_t i = 0; i < mem.memoryHeapCount; i++)
        say("heap %u            %llu MB  %s", i,
            (unsigned long long)(mem.memoryHeaps[i].size >> 20),
            heap_kind(mem.memoryHeaps[i].flags));
    say("memoryTypeCount   %u", mem.memoryTypeCount);

    say("");
    say("--- queues ---------------------------------------------------");
    uint32_t nqf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys[0], &nqf, NULL);
    VkQueueFamilyProperties *qfp = calloc(nqf ? nqf : 1, sizeof *qfp);
    vkGetPhysicalDeviceQueueFamilyProperties(phys[0], &nqf, qfp);
    for (uint32_t i = 0; i < nqf; i++)
        say("family %u          %u queue(s)%s%s%s", i, qfp[i].queueCount,
            (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? " graphics" : "",
            (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT)  ? " compute"  : "",
            (qfp[i].queueFlags & VK_QUEUE_TRANSFER_BIT) ? " transfer" : "");
    /* Upstream reports zero compute queues on purpose, to collapse everything
     * onto the graphics ring. If a compute family shows up here, that decision
     * changed or does not apply to this part. */
    free(qfp);

    say("");
    say("--- limits that bear on the ring work ------------------------");
    say("maxComputeSharedMemorySize      %u", p->limits.maxComputeSharedMemorySize);
    say("maxComputeWorkGroupInvocations  %u", p->limits.maxComputeWorkGroupInvocations);
    say("maxTessellationPatchSize        %u", p->limits.maxTessellationPatchSize);
    say("maxTessellationGenerationLevel  %u", p->limits.maxTessellationGenerationLevel);
    say("maxMemoryAllocationCount        %u", p->limits.maxMemoryAllocationCount);
    say("minUniformBufferOffsetAlignment %llu",
        (unsigned long long)p->limits.minUniformBufferOffsetAlignment);

    say("");
    say("=== probe complete ===");
    say("probe: idling so the log can be read; close from the PS button menu");
    for (;;) sleep(1);
    return 0;
}
