# PS4 Mesa patches

Base: `mesa-build/mesa-ps4` commit `1a59cc147b54e3970814b37313e2f27357672e9d`.
Apply in filename order. 0001 preserves the Gladius detection change that was
already present in the ignored checkout before this session. 0002 implements
placed CPU aliases and reserve/unmap without changing the GPU VA. 0003 fixes
three defects in the ORBIS_DUMP_HUNG instrumentation: the dumper was a
process-wide one-shot that spent itself on a hang that recovered, the
fence-slot wait that the fatal hang actually takes never dumped at all, and the
user-data block stated a fill-overrun conclusion that its own dispatch grid
contradicts. 0004 changes Gladius's raster-config high half from the two-SE
shape to the four-SE shape and provides runtime overrides. The first hardware
run did not exercise that change: its command stream still wrote `0x2a00161a`.
The diagnostic follow-up with 0005 confirmed the Pro calculated and wrote
`0x3a00161a/0x2e` through the direct path, but the user still observed the same
cutscene crash. The raster-config change alone did not fix it. See
docs/HANDOFF-2026-10-09.md before changing the experiment.

0006 adds an opt-in, probe-process-only ACO hook: while
`ORBIS_PROBE_HW_ID=1`, the compute shader's `gl_SubgroupID` is emitted as
`s_getreg_b32 HW_REG_HW_ID` (GFX6–11 selector 4). The Gladius Probe uses that
value to atomically set one bit per observed `(SE, SH, CU)` tuple. The normal
subgroup-ID lowering path is unchanged when the variable is absent. The
instrumented package is `pkgout/probe/` and uploads to `/data/pkg/probe.pkg`.
Two independent Neo runs confirmed the same four-SE, 36-CU bitmap. RADV's
shader dump went to `stderr`, which this console discards. The updated probe
routes ACO assembly through `mesa_log_multiline` to `/data/probe-radv.log`;
retrieve only that known path with the no-login FTP getter. The accepted bitmap
was accompanied by `s_getreg_b32` reading `HW_REG_HW_ID`.

0007 lowers only the hard fence-slot wait deadline to 500 ms so the fatal
hang's label report and IB dump can run before Sony's roughly one-second HP3D
watchdog kills the title. The general wait-report cadence remains five seconds.
The first run confirmed the deadline fires at submission #19369 and identifies
stuck submission #19353, but its stream had been overwritten in the eight-slot
ring. 0008 retains 20 recent streams and limits each slot to 16K dwords (64 KiB),
with an explicit warning if a larger stream cannot be retained. The next run
captured fatal stream #20005 (1312 dwords) through the fence-slot path. This is
diagnostic instrumentation, not a GPU fix; see the handoff for findings.

0009 snapshots the PS/VS program bases at the last `DRAW_INDEX_2` before the CP
progress marker and dumps the first 128 dwords of each image when its address is
inside a live mapping. This avoids attributing a later draw's shader state to
the draw whose execution failed. The 15:49 run confirmed the fatal submit
remains bracketed at dwords 1080-1220 around the 4770-index draw at dword 1097.
It captured PS `0x883274300` and VS program `0x883238300`. The latter is RADV's
VS prolog: it returns to the main VS via `s_setpc_b64 s[2:3]`. Its target,
`0x883271a00`, is passed in user-data registers `0x4e/0x4f` and was not captured
by 0009. The exact PS bytes and prolog disassembly are retained under
`build-ps4-logs/`; read the VS dump only through its return at byte offset
`0x38`.

0010 captures the VS prolog input pointer and dumps the main VS body (resolving
either a direct code pointer or an upload-table pointer). It also checks whether
the indexed draw's index-buffer VA belongs to a live mapping. The fatal fence
slot wait bypassed the earlier stall scanner, so the next package adds 0011:
run the same capture on the fatal timeout path.

0012 extends the timeout capture with a bounded scan of the actual index data,
reporting min/max under both 16-bit and 32-bit interpretations plus the first
32 raw bytes. This separates a valid mapping from corrupt or out-of-range index
contents; the active index type remains a separate context register.

0013 logs the GPU topology RADV actually consumes from the PS4 kernel's
`device_info`: shader engines, arrays per engine, CU masks, and derived CU
counts. This directly compares the driver-reported topology with the hardware
ID probe's 4 x 9 measured topology.

0014 adds an opt-in Neo experiment for the measured sparse CU IDs. The
device bitmap reports `0x3ef` per Neo SE (IDs 0-3 and 5-9), while
`ORBIS_MEASURED_CU_MASK=1` applies it to `spi_cu_en`. Without the variable,
`spi_cu_en` keeps its normal all-ones default. Run the same package first with
the variable absent, then present, to test whether explicitly selecting the
measured sparse IDs changes the Neo hang. The experiment leaves the SE count,
active CU count, and raster configuration unchanged.

0015 adds an opt-in descriptor walk to the fatal fence-slot timeout. Set
`ORBIS_FOLLOW_HUNG=1` to inspect pointers from the retained fatal submission
only; this avoids the thousands of per-frame entries produced by the existing
full-submit `ORBIS_FOLLOW_SH` mode. The walk uses the existing live-mapping and
freed-range checks, and its normal 512-pointer cap applies to the fatal stream.
It is diagnostic only and requires rebuilding/reinstalling the app package.

The working checkout contains the patch series through 0026. Check the newest
with `git -C mesa-build/mesa-ps4 apply --check --reverse ../../src/mesa-patches-ps4/0015-follow-descriptors-on-fatal-hang.patch`.
The checkout is gitignored: these patches preserve its changes. Patch 0005 is
diagnostic only and does not change the raster-config values.

Build the archive using its Mesa development environment, then use
`./scripts/build.sh vkmapprobe --pkg --neo --dev-mesa` for the hardware test.
Host checks exercised the actual mapping functions with shared memfd-backed
views, including failed map/unmap/reservation calls; hardware validation is
required for GPU access and console coherency. Hardware passed GTT, visible VRAM and cached GTT: six GPU copies with zero
mismatches, reserve/remap and cleanup; `DONE tested=3 result=PASS`.

0016 adds an opt-in A/B for the recurring fatal `DRAW_INDEX_2`: with
`ORBIS_SHRINK_DRAW=1`, only packets with a 6144-index bound and exactly 4770
indices are logged and reduced to three indices (one triangle). The default
behavior is unchanged. The current Neo package was built with this patch and
uploaded; the matching runtime switch is enabled in `/data/prospero-win/radv.env`.
Install the uploaded package before launching so the setting reaches code that
contains the hook. A log line beginning `ORBIS_SHRINK_DRAW matched` proves the
experiment actually touched the target draw.

0017 extends the same opt-in predicate to the second failing draw identified by
0016's A run: 5961 indices at VA `0x88a278000`, with the same 6144-index bound.
It keeps the 4770-index target enabled as a control. The next run should log
both `ORBIS_SHRINK_DRAW matched` messages before we decide whether shrinking the
second draw changes the hang.

0018 adds the third draw observed before fatal submit #18504: 2760 indices with
a 4096-index allocation bound at VA `0x88a28b000`. With
`ORBIS_SHRINK_DRAW=1`, this exact draw is also reduced to three indices; the
4770- and 5961-index targets remain enabled. The previous 0017 run's RADV log
did not change during the follow-up launch, so it supplied no fresh GPU dump.
The 0018 Neo package at `/data/pkg/wine-app.pkg` was installed and tested;
SHA-256 `7d463e6e24504b7253fd8959f71d619a9131dde813ccb781b51a4cb811cdf4b3`.
All three `ORBIS_SHRINK_DRAW` targets matched, but submit #19482 still hung at
the same command-stream position. The next draw there was 2232 indices at IB
dword 392, with the same 4096-index bound. Stop adding exact counts to this
predicate: the experiment is moving the observed draw without moving the
failure. Continue from the fresh submit dump and inspect the shader/resource
state instead.

0019 adds a bounded live-mapping check for the first four V#-shaped base fields
in each user-data target during the fatal-only descriptor walk. It reports the
candidate base's current BO, offset and remaining bytes, or that it is outside
every live BO. This is explicitly candidate telemetry, not a descriptor-format
decode or a use-after-free verdict. It is enabled only with
`ORBIS_FOLLOW_HUNG=1` and caps output at 64 candidates per process. The
rebuilt Neo package is uploaded to `/data/pkg/wine-app.pkg`; SHA-256
`8c57ccef7fac0381d54760f190992c388275648f482e4c7d84dc870a494b6f01`. A
known-path no-login FTP RETR matched the local package byte-for-byte.

The 0019 run reproduced the hang at submit #18935. Its draw-bound pixel shader
loads an image-descriptor candidate from SH `0x00e` pointer `0x894600bc0` plus
`0x18`; the eight loaded dwords start `00000000 fffff1ff` and their common
image-base fields decode to `0xff0000000000`, outside live mappings. The log's
`gfx=9` is the raw enum ordinal for `GFX7` in this Mesa tree, not GFX9. The
descriptor's Vulkan binding and whether it caused the hang remain unknown.
The corresponding vertex range is valid in live BO31. This makes an invalid
sampled-image address a candidate, not a proven fault. The earlier suspected
`0x1fc004690000` address was a mistaken read of PM4 words, not the descriptor.
See the handoff before designing a descriptor mutation. The Orbis winsys
currently hardcodes IP version 7.2 in both title modes; that is a driver choice,
not a measurement of Gladius's Polaris-era feature set.

A repeat with the same package again hung at submit #18677 in forced base
identity mode. All three draw-shrink predicates matched, but the 2232-index
draw remained immediately before the stall. The SH `0x00e` pointer moved, but
the sampled-image bytes and decoded `0xff0000000000` base were identical to the
first run. Runtime reported `VK_ERROR_DEVICE_LOST`; see the handoff for capture
paths and the next descriptor-binding trace.

0022 widens 0021's sampled-image update trace from exactly byte offset `0x18`
to offsets in the first 256 bytes of each set, with a 256-entry cap per update
path. The first fatal run with 0021 emitted no trace records, although the
shader's set pointer still contained the suspicious `00000000 fffff1ff` words
at `+0x18`. The widened trace distinguishes a descriptor at another binding
offset from a descriptor region that was not updated through these Vulkan
paths. It is disabled unless `ORBIS_TRACE_GTA_DESC=1` is present.

0023 replaces the per-update cap in 0022 with a mutex-protected trace cache
keyed by set VA, binding, array element and byte offset. It logs every offset,
emits a descriptor only once per key (and again if its contents change), and
caps each path at 512 records. This addresses the 0022 capture, where 256
repeated low-offset template writes consumed the cap before the fatal set at
SH `0x00e` / BO60 offset `0x5c0` could be matched. The trace remains gated by
`ORBIS_TRACE_GTA_DESC=1`.

0024 adds an opt-in trace at descriptor-set bind time. With
`ORBIS_TRACE_GTA_SET=1`, RADV logs each unique bound set VA (up to 256), the
shader stage and descriptor-set SGPR, plus each binding's type, byte offset,
size and array length. This links the fatal SH user-data pointer to its actual
layout even when the descriptor-update-template trace has exhausted its cap.
It does not dump descriptor contents or modify descriptors.

0025 decodes the GFX7 image base from SQ image descriptor words 0 and 1 for
the recurring SH `0x00e` pointer and checks that address against RADV's live
BO mappings during a fatal dump. The generic V# candidate walker cannot answer
this because image descriptors store the base in 256-byte units and use a
different word layout. This distinguishes a bad image address from the earlier
misread of `fffff1ff` at byte offset `0x1c` as a base. It is diagnostic only;
it does not change descriptor contents or GPU state.

0026 adds `ORBIS_TRACE_GTA_IMAGE_OWNER=1`, a narrowly filtered trace for the
sampled-image descriptor pattern found at the fatal SH `0x00e` pointer. At
descriptor writes it records the destination set/binding, source `VkImageView`
and image, view/image formats and extents, sample count, bound image VA, mip
offset, decoded descriptor base and raw words. It covers ordinary descriptor
updates and update templates, and is capped at 32 matching entries per path.
Use it with 0024/0025 to determine whether the mapped descriptor base matches
the Vulkan image backing the bound view. It is diagnostic only.

0027 adds a fatal-only, opt-in dump of the full 288-byte descriptor set when
the pointer is in PS user-data register `0x00e` and binding 0 has the known
GTA image descriptor signature. Set `ORBIS_DUMP_GTA_SET=1` alongside
`ORBIS_FOLLOW_HUNG=1`; this emits all three 96-byte combined-image-sampler
bindings with a live-mapping check and an eight-set cap. The trigger is narrow
so it does not expand routine descriptor logging.

0028 logs the caller and requested size before `linear_alloc_child()` dereferences a null context. The 2026-10-19 Oblivion run faulted at `libvulkan.prx+0xffde23`, the first `ctx->offset` load in this helper, with `si_addr=0x4`; the package's matching link ELF resolves that instruction to `linear_alloc_child`. The trace keeps the fatal behavior unchanged and is intended to identify the caller in the next reproduction.
