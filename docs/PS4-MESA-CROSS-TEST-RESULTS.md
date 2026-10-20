# PS4 Mesa cross-project test results

These are measurements from the prospero-win / RADV port that may be useful to
the `orbis-ports/mesa-ps4` work. They are evidence from a PS4 Pro (Gladius),
not measurements of a base PS4 (Liverpool). In particular, the hardware-ID
result below was collected with the Pro in Neo mode.

## Direct Gladius shader-engine and CU measurement

The Mesa handoff lists `SQ_WAVE_HW_ID` as an unmeasured way to resolve its
shader-engine topology question. We added a probe-only ACO lowering hook that
reads `SQ_WAVE_HW_ID` and atomically records each observed `(SE_ID, SH_ID,
CU_ID)` tuple. Two independent Neo launches reported four shader engines and
the same CU mask, `0x3ef`, on each engine: CU IDs 0–3 and 5–9, or 36 observed
SE/CU pairs total. The probe-only compiler hook is disabled for ordinary
processes.

This result applies to Gladius in Neo mode. It does not establish Liverpool's
physical topology, and a package that makes RADV report Liverpool while
running on a Pro is not a physical Liverpool measurement.

Evidence and implementation:

- `src/mesa-patches-ps4/0006-probe-sq-wave-hw-id.patch`
- `src/mesa-patches-ps4/README.md`, section on patch 0006
- `docs/FINDINGS.md`, “Hardware-ID topology probe prepared and uploaded”

## Low-address placed CPU aliases and GPU access

The Orbis kernel was first tested directly for low-address aliases of the same
Onion and Garlic physical allocations. Aliases at `0x10000000`, `0x20000000`,
`0x40000000`, and `0x60000000` passed bidirectional CPU checks, including
physical-offset aliases; unmapping an alias left the primary mapping usable.

The Mesa Vulkan path then passed placed-map, reserve/unmap, same-address remap,
GPU-copy, and cleanup checks for GTT, visible VRAM, and cached GTT. The probe
reported six 64 KiB GPU copies with zero mismatches. This validates the path
needed to expose GPU-backed storage at low 32-bit process addresses in this
port; it is not a general claim that every PS4 allocation can be aliased.

Evidence and implementation:

- `src/mesa-patches-ps4/0002-orbis-placed-cpu-alias.patch`
- `docs/FINDINGS.md`, “Kernel alias test passed; Mesa implementation built”
  and “Vulkan mapping probe fully passed”
- Hardware log: `build-ps4-logs/ps4-klog-20261009-094758.log` (local, ignored)

## PRX dynamic-TLS failure encountered while loading RADV

In this OpenOrbis PRX build, RADV's imported `__tls_get_addr` returned NULL.
ACO uses C++ `thread_local`, and the null TLS base caused a fault while reading
ACO's thread-local state. This port added a PRX-side wrapper for RADV's TLS
block. Treat this as a toolchain/module-loading integration lead, not as a
general Mesa defect; the behavior may differ in another PS4 executable or
linking arrangement.

Evidence and implementation:

- `src/native/pw_vulkan_radv_ps4.c`, TLS wrapper and comments
- `docs/FINDINGS.md`, “ACO faults on thread-local storage”

## Context

These tests were developed to run 32-bit Windows games through Wine and DXVK,
so the placed-alias work and PRX TLS wrapper address requirements of that
runtime as well as the Vulkan driver. The local Mesa base and patch queue are
recorded in `src/mesa-patches-ps4/README.md`; the local Mesa checkout contains
separate ongoing changes and should not be treated as a clean contribution
branch.
