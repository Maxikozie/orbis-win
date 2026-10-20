# Gladius Bring-Up

**A measurement-first plan for getting RADV — and eventually a Wine runtime — onto a PlayStation 4 Pro, running on Orbis OS itself. No Linux, no dual boot.**

| | |
|---|---|
| Console | PS4 Pro (CUH-7xxx) |
| Firmware | 9.60, GoldHEN working |
| PS4 RADV driver | Native Orbis fork; base mode and Pro Neo path exercised |
| Measured | Liverpool-compatible mode: 18 CU; Neo probe: 4 SE, 36 active CU |
| Still open | Full Vulkan CTS on Gladius; Neo GTA IV GPU hang; generation/feature reporting |

---

The expensive dependency is already built. [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4)
is Mesa's RADV running natively on the PS4 GPU under Orbis OS — a no-DRM winsys arm over
`sceGnmSubmitCommandBuffers`, and a VideoOut WSI. It passes Vulkan CTS on hardware and runs
OpenGothic at roughly 50 fps.

The Pro path has now run on hardware, but it is not complete: a hardware-ID
probe measured the Neo topology, while GTA IV in Neo mode still hangs and a
full Vulkan CTS run on Gladius has not been completed. The non-Neo package runs
in Liverpool-compatible mode and is the working control.

## The generation question remains open

`CHIP_GLADIUS` maps PCI device `0x9924` to the Pro family, and the Orbis
backend changes topology when the title is in Neo mode. However,
`ac_drm_query_hw_ip_info()` currently reports GFX IP version 7.2 in both
modes. The log prints `gfx_level` as a number; `gfx=9` means enum `GFX7`, not
GFX9. That is the current driver configuration, not a silicon measurement.

Sony describes the Pro GPU as combining Polaris features with custom hardware.
That does not establish which Mesa generation and feature paths are correct for
Gladius. Treat its target, descriptor, format/layout, and topology assumptions
as separate questions; use hardware probes and a Gladius Vulkan CTS run to
resolve them rather than inferring support from the family name or CU count.

---

# Stage 1 — Prove the toolchain reaches the television

*No new code. An evening.*

Before anything Gladius-specific, confirm the whole chain works on your box: build host → PKG →
GoldHEN → a frame on screen.

The `orbis-sdk-v1` release bundles the OpenOrbis SDK, the orbis-compat overlay and a matching Mesa
build, pinned together. Take the bundle rather than assembling the three yourself — a Mesa built
against one header tree and linked against another is a struct-layout bug that compiles clean and
fails at runtime. The bundle records the fingerprint of the `include/` tree Mesa compiled against
and refuses to be cut unless it matches.

```sh
# host: x86_64 Linux
tar -xzf orbis-sdk-v1.tar.gz && cd orbis-sdk-v1
./verify.sh                       # offline, no network, no git
. ./env.sh

# hello: no Mesa, no shader compiler — "is the layout right"
cmake -S examples/hello -B /tmp/hello -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/toolchain/orbis-sdk.cmake"
cmake --build /tmp/hello

# triangle: needs both, and puts a frame on the television
cmake -S examples/triangle -B /tmp/tri -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/toolchain/orbis-sdk.cmake"
cmake --build /tmp/tri
```

Package and send it over. GoldHEN's FTP server and remote PKG installer are the delivery path; the
project's own scripts drop into `/data/pkg/` and read back to verify the transfer.

```sh
make-pkg.sh --eboot eboot.bin --out-dir pkgout \
            --title-id TMPS10022 --title "Triangle"
```

**Gate:** `examples/triangle` puts a triangle on your TV. If it does, RADV is already running on
Gladius silicon and every later stage is refinement.

> **Set up the log path now, not later.** Orbis has no usable stdout for this — `freopen` fails on
> the platform. Logging comes back over UDP via `orbis_netlog`, so get a listener running on the
> host before you need it. Diagnosing a black screen with no log is a wasted evening.

---

# Stage 2 — Capture `radeon_info` on Gladius

*No new code. An evening.*

This is the single highest-value thing your hardware can do, and it is a dump. The project's record
contains two `radeon_info` captures — one from the host build, one from a real console — and they
are identical across all 164 fields. That comparison is the whole reason the port is trustworthy.
No such pair exists for Gladius.

Run with `RADV_DEBUG=info` and pull the dump off the UDP relay, which prefixes its lines:

```sh
sed -n 's/^[0-9:.]* radv-info| //p' ps4-udp-*.log > radeon_info-gladius.txt
diff radeon_info-gladius.txt notes/radeon_info-console.txt
```

The Liverpool baseline the record captured, and what a 36-CU Pro would be expected to report. The
right-hand column is inference from CU count, **not measurement** — the point of this stage is to
replace it.

| Field | Liverpool (measured) | Gladius (expected) | Why it matters |
|---|---|---|---|
| `name` | `BONAIRE` | `BONAIRE` | Same gfx7 ISA target |
| `num_cu` | 18 | 36 | The headline difference |
| `max_se` | 2 | 4 ? | Feeds every ring-sizing calculation |
| `num_cu_per_sh` | 9 | 9 ? | If this moved instead of `max_se`, the tess maths differ |
| `family_id` | 120 | — | Confirms the PCI-ID branch fired |
| `vram_size` | 4096 MB | — | Pro has 8 GB total; what the driver is handed is the question |
| `gfx_level` | 9 | 9 | Should not move — if it does, something mis-detected |

**Gate:** `marketing_name` comes back naming Gladius rather than Liverpool. That proves `0x9924`
matched and the driver knows what it is sitting on.

---

# Stage 3 — Find where the two chips diverge

*Tuning, likely no code. A week or two.*

The project's hardest-won Liverpool findings are about ring sizing, and they are exactly the
findings that a doubled compute array puts back in play. Usefully, the author left runtime
overrides on both — you can walk these without rebuilding Mesa.

### `ORBIS_TESS_WORKGROUPS`

Hardcoded to **256** for both parts. CU topology predicts 72 and the hardware overruns the ring
that number sizes. Two register-level explanations were tested on hardware and both failed
(dividing `OFFCHIP_BUFFERING` by `max_se`, and dividing `TF_RING_SIZE` by `max_se` — each left the
write faults at exactly the same addresses).

The prediction is `4 per CU, capped per SE, × max_se`. On a 4-SE part it lands somewhere else
entirely, and 256 was pinned as a *count* against a measured 32768 B per-workgroup stride.

### `ORBIS_TF_BASE_BIAS`

Shifts the tessellation factor ring base address. Exists because a GPU write fault landed `0x10000`
past the ring's end — inside where a second per-engine copy would sit. That fault was measured with
`max_se = 2`. If the ring really is per-SE, four engines move the arithmetic.

### `RADV_DEBUG=no_ib_chaining`

IB chaining is on by default (`can_chain_ib2` is true for gfx7+). A documented knob, not a patch —
reach for it if submission misbehaves before you suspect anything deeper.

### Then run CTS

Run the subset the record already has a baseline for — 735 cases, twice, plus a control run. The
control is not optional: the project's discipline is that *every failure gets attributed by a
control run rather than guessed at*, and that is what keeps a Gladius-specific defect from being
confused with a pre-existing one.

**Gate:** Gladius CTS verdicts match the Liverpool baseline, or you have a short list of cases that
differ — a genuine finding either way, and the first thing this project has ever learned about the
Pro.

> **The failure mode this project keeps hitting:** "A path that looks right, pointing at a driver
> from another day, with nothing printing the date." Three live instances in two days. The rule
> that catches all of them: **make the check print what it measured**, not a verdict.
>
> Also from the record: the console's clock ran ~46 minutes ahead of the host, which turned an
> mtime comparison into a confident wrong conclusion.

---

# Stage 4 — Run a real title, get a performance number

*No new code. A day.*

OpenGothic is the project's reference workload: 3,328 frames on a base PS4 with zero protection
faults, zero timeouts, ~50 fps. Build the `ps4-support` branch and run the same thing on the Pro.

This is the stage that tells you whether the whole idea is worth pursuing, because it measures the
thing the Pro is actually weak at. The Pro's GPU is roughly double Liverpool's. Its CPU is the
*same Jaguar core at a third higher clock* — 2.13 GHz against 1.6.

- If OpenGothic scales well, you were GPU-bound and the Pro is a real upgrade.
- If it barely moves, you were CPU-bound — and that matters enormously for stage 5, because Wine is
  the most CPU-bound workload you could pick.

**Gate:** A frame-rate delta against the ~50 fps Liverpool figure. Either answer is decision-grade.

---

# Stage 5 — Only now: the prospero-win port

*Nobody has done this. Months.*

Measured from the repository, so the shape of the job is known even though no one has attempted it:

| Component | Size | Status on PS4 |
|---|---|---|
| `src/` — the i386 translator | 13,519 LOC | **Ports free** |
| `native/` + `wine/ps5/` + `wine/wineps5/` | 10,748 LOC | Rewrite against PS4 APIs |
| `wine/patches/` | 81 patches | Constants must be re-measured on Orbis |
| `native/pw_agc_ps5.c` — the presenter | 164 LOC | AGC → GNM. Genuinely small. |

The translator is the surprise, and it is good news. It is the hardest intellectual work in the
repository and it needs no porting:

- `pw_x86_reencode.c` already runtime-detects BMI2 through CPUID and takes a fallback path when it
  is absent — exactly Jaguar's situation.
- Host intrinsics are SSE2-only (`_mm_loadu_si128`, `_mm_storeu_si128`, `clflush`, `mfence`).
- `pw_x86_cpuid_mask` already hides AVX, AVX2, FMA, F16C, BMI1/2 and AVX-512 from the guest.

Jaguar was never planned for, but nothing in it was assumed away either.

The real work is the 81 Wine patches. These are not ported constants — they are *measured* ones:
signal-context and FP-state offsets, `PAGE = 0x4000`, direct-memory-for-anonymous-memory, the
in-process wineserver. Each needs redoing against Orbis, by measurement, on hardware. That is the
months.

One patch is already working in your favour: `0110-server-run-in-process-on-ps5.patch` runs
wineserver in-process rather than over IPC. On Jaguar, whose two 4-core clusters talk to each other
slowly, that is precisely the mitigation the architecture needs.

**Honest ceiling:** Source-era and D3D9 titles at 1080p. Not GTA IV — the PS5 figures in that
README come off a Zen 2 core at 3.5 GHz. But a PS4 Pro booting to its own home screen and launching
Half-Life 2 is the actual prize, and nothing found so far says it is out of reach.

---

## What to do this week

Stages 1 and 2 are both an evening, need no new code, and together answer the only question that
gates everything else: **does the existing PS4 Vulkan driver come up on Gladius silicon?**

Get the triangle on screen, capture `radeon_info`, and diff it against the Liverpool dump in
`ps4-mesa-docs/notes/`.

That diff is worth posting back to the project whatever it says. You would be the first person to
run this driver on a Pro.

---

## Sources

- [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4) — RADV on PS4 (branch `orbis`)
- [orbis-ports/ps4-mesa-docs](https://github.com/orbis-ports/ps4-mesa-docs) — HANDOFF, PLAN, `research/01-submission.md`, `notes/radeon_info-console.txt`
- [orbis-sdk-v1](https://github.com/orbis-ports/orbis-porting-kit/releases/tag/orbis-sdk-v1) — the paired SDK tarball
- [orbis-ports/orbis-compat](https://github.com/orbis-ports/orbis-compat) — platform overlay, packaging, console logging
- [mpereiraesaa/prospero-win](https://github.com/mpereiraesaa/prospero-win) — the PS5 original

Liverpool figures and CTS baselines are quoted from the project's own record. Gladius columns
marked `?` are inference awaiting measurement.

`orbis-compat` and `orbis-porting-kit` are both archived in favour of an `orbsdk` that is not
public yet — the `orbis-sdk-v1` tarball is the working entry point today.
