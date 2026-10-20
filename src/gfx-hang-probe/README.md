# Gladius graphics hang probe

This is a standalone Vulkan graphics workload for the PS4 Pro Neo investigation.
It presents for six minutes and reports progress every ten seconds, so a GPU
hang should be visible much sooner than waiting for GTA IV to reach its intro
cutscene.

The recurring GTA submission had a `DRAW_INDEX_2` of 2,232 UINT16 indices,
maximum index 909, a 36-byte vertex stride and 910 records. This probe uses
those draw dimensions, a normal vertex fetch, and a pixel shader that samples
a small, initialized image through a valid combined-image-sampler descriptor.
It records 64 identical indexed draws per presented frame and waits for each
frame to complete before reporting it. The app logs elapsed time and completed
draw count every ten seconds during its six-minute run.

The GTA capture also contained a descriptor-shaped value decoding to
`0xff0000000000`, outside RADV's mapped VA range. Its Vulkan binding and role
remain unknown. This probe intentionally uses a valid descriptor and therefore
does not reproduce that suspicious value or the proprietary GTA shaders. A
pass rules out only this standalone workload; a hang narrows the fault to a
small public Vulkan path and gives a short, repeatable diagnostic target.

Build a Neo package with the local diagnostic Mesa and package support modules:

```sh
scripts/build.sh gfxhang --pkg --neo --dev-mesa
scripts/build.sh gfxhang-base --pkg --dev-mesa
```

The Neo package uses title ID `GLDS10012` (“Gladius GFX Hang Probe”); the base
package uses `GLDS10013` (“Gladius GFX Hang Probe Base”), so both can be
installed side by side. Logs are written through `sceKernelDebugOutText` and
stdout. Leave the klog receiver running before launch. The console must have the
corresponding `libc.prx` and `libSceFios2.prx` in `sce_module/` when packaging.

For a one-draw control that submits one frame and then idles indefinitely, build:

```sh
scripts/build.sh gfxonce --pkg --neo --dev-mesa
scripts/build.sh gfxonce-base --pkg --dev-mesa
scripts/build.sh gfxrepeat-base --pkg --dev-mesa
scripts/build.sh gfxrepeat-neo --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-solid --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-vbo --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index3 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-lowidx --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index300 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index1266 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index783 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index541 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index662 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index723 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index753 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index768 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index776 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index772 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index770 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index769 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-points257 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-points256 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-base-points257 --pkg --dev-mesa
scripts/build.sh gfxrepeat-neo-wd-eop-points257 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-no-ia-eoi-points257 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-wd-eop-index769 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index772 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index770 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-index769 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-points257 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-neo-points256 --pkg --neo --dev-mesa
scripts/build.sh gfxrepeat-base-points257 --pkg --dev-mesa
```

These use title IDs `GLDS10014` (Neo) and `GLDS10015` (base). They retain the
same pipeline, descriptor, vertex/index buffers, and single 2,232-index draw,
but remove the 64-draw loop and subsequent frames. A watchdog here points at
the first frame's draw/present submission. A completed-frame message means the
draw, present call, and queue-idle wait returned; it does not by itself prove
that the triangle reached scanout. If the screen stays black, investigate
rendered contents and presentation/scanout separately from GPU completion. The app intentionally remains alive
after `PASS` to preserve its logs, so it will not close on its own. Closing it from the PS button menu triggered
`CPU_FAULT_SUBMITDONE_TIMEOUT_IN_SUSPEND_ASYNC` (`0xa0d0c00a`) in the 08:49:12 base run. Treat that as a separate
suspend-path failure, not evidence that the one-shot draw itself failed.

`gfxrepeat-base` (`GLDS10016`) uses the same indexed/textured draw once per
frame for a 60-second run. It distinguishes a single-present issue
from a draw-path issue without the 64-draw stress loop.
`gfxrepeat-neo` (`GLDS10017`) is the matching Neo package for the direct A/B.
`gfxrepeat-neo-solid` (`GLDS10019`) keeps the indexed vertex fetch and descriptor
binding but uses a constant-color fragment shader, isolating texture sampling.
`gfxrepeat-neo-vbo` (`GLDS10020`) uses the same vertex buffer and solid fragment
but submits a nonindexed `vkCmdDraw(3)`, removing indexed fetch from the draw.
`gfxrepeat-neo-index3` (`GLDS10021`) submits only indices 0, 1, and 2; it passed
60 seconds on Neo with 3,544 completed frames. This establishes that short
indexed draws work. `gfxrepeat-neo-lowidx` (`GLDS10022`) keeps the full
2,232-index draw but restricts values to 0..2; it immediately hangs the Neo
GPU, so high vertex indices are not required for the failure. `gfxrepeat-neo-index300`
(`GLDS10023`) passes with 300 indices, all in the range 0..2. `gfxrepeat-neo-index1266`
(`GLDS10024`) hangs with 1,266 such indices and hits the GPU submit-done
timeout. `gfxrepeat-neo-index783` (`GLDS10025`) also hangs. The boundary is
above 768 and at or below 783: 768 indices pass for 60 seconds (670 completed
frames), while 783 hangs. `gfxrepeat-neo-index776` (`GLDS10031`) and `gfxrepeat-neo-index772`
(`GLDS10032`) fail before their first completed frame with HP3D watchdog
timeouts; 772 reaches the 45-second GPU submit-done timeout. The boundary is
above 768 and at or below 772. `gfxrepeat-neo-index770` (`GLDS10033`) also reaches the 45-second GPU submit-done timeout, so the boundary is between 769 and 770. `gfxrepeat-neo-index769` (`GLDS10034`) also fails with a 45-second GPU submit-done timeout, while 768 passes. The exact cutoff is 769 indices. `gfxrepeat-neo-points257` (`GLDS10035`) also times out and crashes, ruling out a triangle-list-only cause. `gfxrepeat-neo-points256` (`GLDS10036`) passes 60 seconds (3,559 frames), confirming the 257-point cutoff on Neo. `gfxrepeat-base-points257` (`GLDS10037`) passes 60 seconds with 3,552 frames. The same 257-point draw is healthy in base mode but crashes in Neo, isolating the failure to Neo-specific draw setup. Frame logs report
`draw-calls`, not indexed draws, because the same counter is used by indexed
and nonindexed variants.
