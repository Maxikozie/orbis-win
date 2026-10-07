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
#include <time.h>
#include <unistd.h>

#include <vulkan/vulkan.h>
#include <orbis/libkernel.h>

#include "bench_comp_spv.h"

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


/* ------------------------------------------------------------------ bench --
 * Measure achieved FMA throughput, which is a fact about the silicon rather
 * than about what ac_orbis_drm.c asserts.
 *
 * Reference points, 2 flops per FMA lane per clock, 64 lanes per CU:
 *   base PS4   18 CU @ 800 MHz  ->  18 * 64 * 2 * 0.800 = 1843 GFLOP/s
 *   PS4 Pro    36 CU @ 911 MHz  ->  36 * 64 * 2 * 0.911 = 4196 GFLOP/s
 * Real kernels land under peak, but the two are far enough apart that a
 * measurement says which machine is underneath.
 */
static uint32_t find_mem(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void run_bench(VkPhysicalDevice phys, uint32_t qf)
{
    say("");
    say("--- compute throughput ---------------------------------------");

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
    };
    VkDevice dev;
    if (vkCreateDevice(phys, &dci, NULL, &dev) != VK_SUCCESS) {
        say("bench: vkCreateDevice FAILED - skipping"); return;
    }
    VkQueue q; vkGetDeviceQueue(dev, qf, 0, &q);
    say("bench: ok   vkCreateDevice + queue");

    enum { WG_SIZE = 64, MAX_WG = 8192 };
    const VkDeviceSize bytes = (VkDeviceSize)MAX_WG * WG_SIZE * sizeof(float);
    say("bench: buffer %llu B for up to %u workgroups", (unsigned long long)bytes, (unsigned)MAX_WG);

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buf;
    if (vkCreateBuffer(dev, &bci, NULL, &buf) != VK_SUCCESS) {
        say("bench: vkCreateBuffer FAILED"); vkDestroyDevice(dev, NULL); return;
    }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, buf, &mr);
    uint32_t mt = find_mem(phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == UINT32_MAX) mt = find_mem(phys, mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = mt,
    };
    VkDeviceMemory mem;
    if (vkAllocateMemory(dev, &mai, NULL, &mem) != VK_SUCCESS) {
        say("bench: vkAllocateMemory FAILED (%llu B)", (unsigned long long)mr.size);
        vkDestroyBuffer(dev, buf, NULL); vkDestroyDevice(dev, NULL); return;
    }
    vkBindBufferMemory(dev, buf, mem, 0);

    VkDescriptorSetLayoutBinding dslb = {
        .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &dslb,
    };
    VkDescriptorSetLayout dsl;
    if (vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl) != VK_SUCCESS) {
        say("bench: vkCreateDescriptorSetLayout FAILED"); return; }
    say("bench: ok   descriptor set layout");

    VkPushConstantRange pcr = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                .offset = 0, .size = sizeof(uint32_t) };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr,
    };
    VkPipelineLayout pl;
    if (vkCreatePipelineLayout(dev, &plci, NULL, &pl) != VK_SUCCESS) {
        say("bench: vkCreatePipelineLayout FAILED"); return; }
    say("bench: ok   pipeline layout");

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof bench_comp_spv, .pCode = bench_comp_spv,
    };
    VkShaderModule sm;
    if (vkCreateShaderModule(dev, &smci, NULL, &sm) != VK_SUCCESS) {
        say("bench: vkCreateShaderModule FAILED"); return;
    }
    VkComputePipelineCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main" },
        .layout = pl,
    };
    VkPipeline pipe;
    if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe) != VK_SUCCESS) {
        say("bench: vkCreateComputePipelines FAILED"); return;
    }
    say("bench: ok   compute pipeline");

    VkDescriptorPoolSize dps = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &dps,
    };
    VkDescriptorPool dp;
    if (vkCreateDescriptorPool(dev, &dpci, NULL, &dp) != VK_SUCCESS) {
        say("bench: vkCreateDescriptorPool FAILED"); return; }
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl,
    };
    VkDescriptorSet ds;
    if (vkAllocateDescriptorSets(dev, &dsai, &ds) != VK_SUCCESS) {
        say("bench: vkAllocateDescriptorSets FAILED"); return; }
    say("bench: ok   descriptor set");
    VkDescriptorBufferInfo dbi = { .buffer = buf, .offset = 0, .range = VK_WHOLE_SIZE };
    VkWriteDescriptorSet wds = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds, .dstBinding = 0,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &dbi,
    };
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

    VkCommandPoolCreateInfo cpi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qf,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    VkCommandPool cp;
    if (vkCreateCommandPool(dev, &cpi, NULL, &cp) != VK_SUCCESS) {
        say("bench: vkCreateCommandPool FAILED"); return; }
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cp,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(dev, &cbai, &cb) != VK_SUCCESS) {
        say("bench: vkAllocateCommandBuffers FAILED"); return; }
    say("bench: ok   command buffer");
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence; vkCreateFence(dev, &fci, NULL, &fence);

    /* ⚠ KEEP EACH DISPATCH WELL UNDER THE GPU WATCHDOG. The port's own record
     * notes a watchdog it has never reached because no wait hit five seconds;
     * these are sized for a few hundred ms. */
    /* ⚠ SMALL, AND IT ANNOUNCES EACH STEP BEFORE TAKING IT.
     *
     * The first version logged a size only AFTER its timed pass completed, so
     * when the console went down mid-sweep the log named nothing at all and the
     * run taught us only that something died. klog replays its buffer on
     * connect, so a line printed BEFORE a dispatch survives even a hang — which
     * makes the next failure name itself.
     *
     * Sizes also start far below anything that saturates and stop at the first
     * dispatch that misbehaves, rather than escalating into whatever killed the
     * previous run. */
    const uint32_t iters = 200;

    say("bench: %u iters x 4 FMA per invocation, 64 threads per workgroup", iters);
    say("bench: stopping at the first dispatch that does not return cleanly");
    say("   %-10s %-12s %-12s %s", "workgroups", "invocations", "time ms", "GFLOP/s");

    double best = 0.0;
    const uint32_t sizes[] = { 1, 4, 16, 64, 256, 1024, 4096 };

    for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        const uint32_t wg = sizes[si];
        int aborted = 0;

        for (int pass = 0; pass < 2 && !aborted; pass++) {
            say("bench: -> dispatch %u workgroup(s), pass %d", wg, pass);

            vkResetCommandBuffer(cb, 0);
            VkCommandBufferBeginInfo bi = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) {
                say("bench: vkBeginCommandBuffer FAILED"); aborted = 1; break;
            }
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
            vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof iters, &iters);
            vkCmdDispatch(cb, wg, 1, 1);
            if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
                say("bench: vkEndCommandBuffer FAILED"); aborted = 1; break;
            }

            vkResetFences(dev, 1, &fence);
            VkSubmitInfo si2 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .commandBufferCount = 1, .pCommandBuffers = &cb };

            const double t0 = now_sec();
            VkResult sr = vkQueueSubmit(q, 1, &si2, fence);
            if (sr != VK_SUCCESS) {
                say("bench: vkQueueSubmit -> %d, stopping", (int)sr); aborted = 1; break;
            }
            /* 3s, not 10: a hang should be reported while the console is still
             * answering, not after it has gone. */
            VkResult w = vkWaitForFences(dev, 1, &fence, VK_TRUE, 3000000000ull);
            const double dt = now_sec() - t0;

            if (w != VK_SUCCESS) {
                say("bench: wait -> %d after %.0f ms at %u workgroup(s) - STOPPING",
                    (int)w, dt * 1e3, wg);
                aborted = 1; break;
            }

            if (pass == 1) {
                const double flops = (double)wg * WG_SIZE * (double)iters * 4.0 * 2.0;
                const double gfs   = flops / dt * 1e-9;
                if (gfs > best) best = gfs;
                say("   %-10u %-12u %-12.2f %.1f", wg, wg * WG_SIZE, dt * 1e3, gfs);
            }
        }
        if (aborted) break;
    }

    /* ------------------------------------------------------------------
     * ⚠ THE RAMP ABOVE MEASURES SUBMIT OVERHEAD, NOT THE GPU.
     *
     * Every size in it came back in 0.7-2 ms and 4096 workgroups finished
     * FASTER than 1024 — which is the signature of a fixed cost swamping the
     * work. The submit-and-fence floor on this console is ~0.7 ms; at 4096
     * workgroups x 200 iters the dispatch is only ~419 MFLOP, perhaps 0.2 ms of
     * real compute. The ramp is worth keeping as a stability check and worth
     * nothing as a throughput number.
     *
     * So: hold the dispatch at its widest and raise the per-invocation work
     * until the fixed cost is noise. Reporting several iteration counts rather
     * than one makes the answer self-checking — if GFLOP/s climbs and then
     * flattens, the flat part is the hardware. If it never flattens, something
     * other than ALU throughput is the limit and the number is not peak. */
    if (best > 0.0) {
        say("");
        say("bench: holding 4096 workgroups and raising iterations until");
        say("       the ~0.7 ms submit floor stops mattering");
        say("   %-10s %-12s %-12s %s", "iters", "GFLOP", "time ms", "GFLOP/s");

        const uint32_t deep[] = { 2000, 10000, 40000, 100000 };
        const uint32_t wg = 4096;

        for (unsigned di = 0; di < sizeof deep / sizeof deep[0]; di++) {
            const uint32_t it = deep[di];
            say("bench: -> dispatch %u workgroups x %u iters", wg, it);

            vkResetCommandBuffer(cb, 0);
            VkCommandBufferBeginInfo bi = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            vkBeginCommandBuffer(cb, &bi);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
            vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof it, &it);
            vkCmdDispatch(cb, wg, 1, 1);
            vkEndCommandBuffer(cb);

            vkResetFences(dev, 1, &fence);
            VkSubmitInfo si3 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .commandBufferCount = 1, .pCommandBuffers = &cb };
            const double t0 = now_sec();
            if (vkQueueSubmit(q, 1, &si3, fence) != VK_SUCCESS) {
                say("bench: submit failed at %u iters, stopping", it); break;
            }
            /* 4s: the longest of these should be well under one second, so a
             * wait that reaches the timeout means a hang rather than slowness. */
            VkResult w = vkWaitForFences(dev, 1, &fence, VK_TRUE, 4000000000ull);
            const double dt = now_sec() - t0;
            if (w != VK_SUCCESS) {
                say("bench: wait -> %d after %.0f ms at %u iters - STOPPING",
                    (int)w, dt * 1e3, it);
                break;
            }
            const double flops = (double)wg * WG_SIZE * (double)it * 4.0 * 2.0;
            const double gfs   = flops / dt * 1e-9;
            if (gfs > best) best = gfs;
            say("   %-10u %-12.1f %-12.1f %.1f", it, flops * 1e-9, dt * 1e3, gfs);
        }
    }

    /* ------------------------------------------------------------------
     * THE SCALING KNEE — how many compute units actually absorb work.
     *
     * Throughput in GFLOP/s cannot be converted to a CU count, because it also
     * contains the GPU clock and the kernel's issue efficiency, and neither is
     * known here: the converged 985 GFLOP/s is only 53% of the 18-CU reference,
     * so something in that product is not what the reference assumes.
     *
     * Where the curve FLATTENS does not care about either. Add wavefronts until
     * throughput stops rising; the point it stops is set by how many SIMDs are
     * there to fill. Clock and efficiency scale the whole curve up or down and
     * move the knee not at all.
     *
     *   18 CU =  72 SIMDs
     *   36 CU = 144 SIMDs      — a factor of two in the knee position
     *
     * ⚠ EACH POINT MUST STAY OUT OF THE SUBMIT FLOOR, and small dispatches fall
     * into it at a fixed iteration count — that is what made the first sweep
     * meaningless. So the work per dispatch is held roughly constant by scaling
     * iterations against the workgroup count, which also keeps the unsaturated
     * points (slow by construction) from running long. */
    if (best > 0.0) {
        say("");
        say("--- scaling knee: where does adding wavefronts stop helping? ---");
        say("   18 CU = 72 SIMDs, 36 CU = 144 SIMDs. The KNEE is the answer,");
        say("   not the height — clock and efficiency move height, not knee.");
        say("   %-8s %-10s %-10s %-11s %s", "wgroups", "waves", "iters", "time ms", "GFLOP/s");

        const uint32_t knee[] = { 8, 16, 32, 64, 96, 128, 192, 256, 384,
                                  512, 768, 1024, 1536, 2048, 3072, 4096 };
        /* ~20 GFLOP per dispatch: 20e9 / (wg * 64 lanes * 8 flops) */
        const double target_flops = 20.0e9;

        for (unsigned ki = 0; ki < sizeof knee / sizeof knee[0]; ki++) {
            const uint32_t wg = knee[ki];
            double want = target_flops / ((double)wg * WG_SIZE * 8.0);
            if (want < 2000.0)    want = 2000.0;
            if (want > 5000000.0) want = 5000000.0;
            const uint32_t it = (uint32_t)want;

            vkResetCommandBuffer(cb, 0);
            VkCommandBufferBeginInfo bi = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            vkBeginCommandBuffer(cb, &bi);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
            vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof it, &it);
            vkCmdDispatch(cb, wg, 1, 1);
            vkEndCommandBuffer(cb);

            vkResetFences(dev, 1, &fence);
            VkSubmitInfo si4 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .commandBufferCount = 1, .pCommandBuffers = &cb };
            const double t0 = now_sec();
            if (vkQueueSubmit(q, 1, &si4, fence) != VK_SUCCESS) {
                say("knee: submit failed at %u workgroups, stopping", wg); break;
            }
            VkResult w = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
            const double dt = now_sec() - t0;
            if (w != VK_SUCCESS) {
                say("knee: wait -> %d at %u workgroups after %.0f ms - STOPPING",
                    (int)w, wg, dt * 1e3);
                break;
            }
            const double flops = (double)wg * WG_SIZE * (double)it * 4.0 * 2.0;
            say("   %-8u %-10u %-10u %-11.1f %.1f",
                wg, wg, it, dt * 1e3, flops / dt * 1e-9);
        }
        say("");
        say("   read it as: the last workgroup count whose GFLOP/s is still");
        say("   meaningfully above the previous one. 1 workgroup = 1 wavefront.");
    }

    say("");
    say("bench: peak %.0f GFLOP/s", best);
    say("   base PS4  18 CU @ 800 MHz  = 1843 GFLOP/s peak");
    say("   PS4 Pro   36 CU @ 911 MHz  = 4196 GFLOP/s peak");
    say("   driver claims %u CU", 18u);

    vkDestroyFence(dev, fence, NULL);
    vkDestroyCommandPool(dev, cp, NULL);
    vkDestroyDescriptorPool(dev, dp, NULL);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyShaderModule(dev, sm, NULL);
    vkDestroyPipelineLayout(dev, pl, NULL);
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    vkFreeMemory(dev, mem, NULL);
    vkDestroyBuffer(dev, buf, NULL);
    vkDestroyDevice(dev, NULL);
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

    /* ⚠ DOES OUR setenv REACH MESA'S getenv? ORBIS_NUM_SE=4 was applied from
     * /data/orbis-env.txt and the driver still reported shaderEngineCount=2,
     * even though ac_gpu_info.c:1249 is a bare
     *     info->max_se = device_info->num_shader_engines;
     * with no clamp, and the ORBIS_NUM_SE string is present in this bundle's
     * libvulkan_radeon.a. Either the write is not visible to Mesa or something
     * downstream overrides it, and a read-back from this same binary separates
     * those two without another guess.
     *
     * RADV is statically linked here, so this process and Mesa share one musl
     * and one environ. If this prints 4 and the driver still says 2, the write
     * is fine and the override is inside Mesa. */
    {
        static const char *const knobs[] = { "ORBIS_NUM_SE", "ORBIS_CHIP",
                                             "ORBIS_TCC_BLOCKS", "RADV_DEBUG" };
        for (unsigned i = 0; i < sizeof knobs / sizeof knobs[0]; i++) {
            const char *v = getenv(knobs[i]);
            say("probe: getenv(%s) = %s", knobs[i], v ? v : "(null)");
        }
    }

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

    run_bench(phys[0], 0);

    say("");
    say("=== probe complete ===");
    say("probe: idling so the log can be read; close from the PS button menu");
    for (;;) sleep(1);
    return 0;
}
