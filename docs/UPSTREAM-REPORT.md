# For the mesa-ps4 maintainer: Gladius reports 18 CUs on a Pro, and it is masking them

Measured on a PlayStation 4 **Pro**, firmware 9.60, GoldHEN, 2026-10-06/07.

Your `ac_orbis_drm.c` says, of the shader-array topology:

> ALL UNCITED […] a shape is reported and named as a guess. A measurement would
> replace it: SQ_WAVE_HW_ID carries SE_ID/SH_ID/CU_ID […] Until it has run,
> treat every occupancy number RADV derives from these as provisional.

and of the Gladius raster config:

> Unresolved without the hardware, and it only affects the Pro.

This is that measurement, by a different route — occupancy scaling rather than
SQ_WAVE_HW_ID, so it needs no new GPU code.

## The short version

`num_shader_engines = 2` and `device_id = 0x9920` are unconditional. `cu_bitmap`
is built from the former and RADV programs `COMPUTE_STATIC_THREAD_MGMT` from
`cu_bitmap` — so on a Pro the driver is not merely *naming* half the part, it is
**configuring** half the part. Selecting Gladius from `sceKernelIsNeoMode()`
doubles measured compute throughput.

## Method

A Vulkan compute kernel of four independent FMA chains per invocation, 64-thread
workgroups, result stored so it cannot be optimised away. Throughput is sampled
against workgroup count. The **knee** — where adding wavefronts stops helping —
is the answer; it is set by how many SIMDs exist, and is independent of GPU
clock and ALU efficiency, which scale the curve's height without moving it.

Per-wavefront rate while every wave still has a SIMD to itself: **6.125 GFLOP/s**.
`%lin` is measured throughput against that straight line.

| waves | stock driver | %lin | with Gladius | %lin |
|---|---|---|---|---|
| 8 | 48.9 | 100% | 49.0 | 100% |
| 16 | 97.5 | 99% | 97.5 | 99% |
| 32 | 195.8 | 100% | 194.0 | 99% |
| 64 | 384.3 | 98% | 384.7 | 98% |
| **96** | 464.6 | **79%** | 571.2 | **97%** |
| **128** | 623.8 | **80%** | 769.0 | **98%** |
| 192 | 689.4 | 59% | 908.5 | 77% |
| 256 | 739.1 | 47% | 1250.3 | 80% |

Stock: linear to 64, broken by 96 → between **64 and 96 SIMDs**. 72 (18 CU × 4)
fits; 144 does not.
Gladius: linear to 128, broken by 192 → between **128 and 192 SIMDs**. 144
(36 CU × 4) fits; 72 is nowhere near.

Two independent confirmations:

```
plateau   950 -> 1830 GFLOP/s  (1.93x)

  18 CU @ 800 MHz: theoretical 1843, measured  950  -> 51.5% efficiency
  36 CU @ 911 MHz: theoretical 3686, measured 1830  -> 49.6% efficiency
```

Essentially **unchanged efficiency on twice the hardware**, which is what
doubling a compute array looks like and is not what a mislabelled constant looks
like. It also suggests the GPU clock is nearer 800 MHz than the Pro's nominal
911 — at 911 the efficiency would have to be 44% and would have had to *drop* on
the larger configuration.

## The change

`patches/0001-orbis-gladius-neo-detection.patch`, 43 lines. Gated on
`sceKernelIsNeoMode()` rather than on "is this a Pro", because a Pro running a
non-Neo title genuinely only has 18 CUs and reporting 36 there would mis-size
every ring. `ORBIS_FORCE_NEO=0/1` overrides it so the A/B needs no rebuild.

⚠ The title's package must also set `param.sfo` `ATTRIBUTE` bit 24
(`0x00800000`). Without it a Pro runs the title in base-PS4 compatibility mode
and `sceKernelIsNeoMode()` returns 0 — which is correct behaviour, and worth
knowing because every homebrew package built without that bit has been getting
half the GPU regardless of this patch.

## What this does NOT establish

- **`CHIP_GLADIUS`'s `raster_config` pair is still unresolved.** A triangle
  renders correctly with it, but one full-screen triangle barely exercises
  cross-engine work distribution, which is what that register governs. Your
  comment describes the failure mode as "quarter-of-the-screen shaped"; I have
  not run anything that would show it. Treat the pair as untested, not vindicated.
- **No CTS run** on the Gladius path. Only the compute benchmark and the
  bundle's triangle example.
- **SQ_WAVE_HW_ID still has not been read.** The knee says how many SIMDs absorb
  work; it does not enumerate `[se][sh]` the way `cu_bitmap` wants. If the real
  topology is not 4×1×9, this measurement cannot tell you.
- `num_shader_arrays_per_engine = 1` and `num_cu_per_sh = 9` were left alone.
  Only the engine count changed, so 4×1×9 is an assumption that happens to
  produce the right total.

## Reproducing it

Everything is at `github.com/plastikman/ps4-gladius` — the patch, the probe that
produces these tables, and `docs/FINDINGS.md`, which is the full record including
the wrong turns.

Measurements and console runs are mine, on my own hardware; the analysis and the
patch were drafted with Claude.
