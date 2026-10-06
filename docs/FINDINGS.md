# Findings

Newest last. Things that cost time, and what settled them.

---

## 2026-10-06 — Stage 1 host chain works end to end

Debian 13 (trixie), `clang 19.1.7`. Built `hello` and `triangle` from
`orbis-sdk-v1` and produced installable `.pkg` files for both. Nothing has been
near the console yet — this is the host half of stage 1 only.

| | eboot.bin | .pkg |
|---|---|---|
| `hello` | 660 KB | 5.8 MB |
| `triangle` | 25 MB | 26 MB |

The size gap is the whole Vulkan driver. `triangle`'s ELF carries **1,782**
`radv_*` / `ac_drm_*` / `aco_*` symbols, which is the cheapest proof that Mesa
actually linked rather than silently falling out of the link line.

Bundle checksum, as downloaded:

```
f4f079a4df334b46a76996f895283904283d6a006f97102aafaa53d4ecfc9622  orbis-sdk-v1.tar.gz
```

### clang 19 builds an LLVM-18 bundle clean

`BUNDLE.txt` pins `openorbis-sdk-asset=toolchain-llvm-18.tar.gz`, so LLVM 18 is
the reference. Debian 13 ships 19 and there is no apt path to 18 here. It
builds, links and packages without a single diagnostic. The SDK ships libc and
libc++ prebuilt; the host compiler only compiles our code and drives `ld.lld`,
so the version coupling is looser than it looks.

**Not proven on hardware.** If the console behaves oddly in a way that smells
like codegen or TLS layout, this is the first thing to rule out, by pinning 18.

### ⚠ `gate=unproven` is a false warning — upstream bug

Every configure prints:

> `BUNDLE.txt says gate=unproven: nothing has been built from this bundle yet`

`BUNDLE.txt` line 31 says `gate=pass`. The check in `toolchain/orbis-sdk.cmake:47`
is a whole-file regex:

```cmake
file(READ "${ORBIS_SDK_BUNDLE}/BUNDLE.txt" _orbis_manifest)
if(_orbis_manifest MATCHES "gate=unproven")
```

and the comment three lines above the value reads *"A bundle carrying
gate=unproven is not publishable."* The regex matches the **comment**. It needs
line anchoring. Cosmetic, but it trains you to ignore warnings from that file,
which is the actual cost. `scripts/build.sh` filters this one block specifically
so a real warning still stands out.

### ⚠ `examples/triangle` does not build from the bundle alone

```
ORBIS_KIT_DIR=<bundle> holds no vkloader/
```

The example resolves `ORBIS_KIT_DIR` to `${CMAKE_CURRENT_LIST_DIR}/../..` — the
bundle root — and expects `vkloader/CMakeLists.txt` under it. The bundle has no
top-level `vkloader/`; it carries the same code at `orbis-compat/vkloader/`.
The example was written to live inside an `orbis-porting-kit` checkout, and the
bundle cut it loose from that.

Fix, which `scripts/build.sh` applies:

```sh
-DORBIS_KIT_DIR="$BUNDLE/orbis-compat"
```

The example's own comment says it deliberately wants *the kit's* vkloader rather
than orbis-compat's, so that the kit's copy is the one under test. That
distinction does not matter to us — we are not testing the kit — but it is why
the default points where it does.

### Two bugs of our own, both in the verification code

Worth recording because both produced a **failure report on a check that
passed**, which is the expensive direction.

1. **`grep -q` under `set -o pipefail`.** `llvm-nm … | grep -q _sceProcessParam`
   reported missing on an image that had it. `grep -q` exits at the first match,
   `llvm-nm` takes SIGPIPE, and `pipefail` surfaces the pipeline as failed. Now
   the symbol table is captured once into a variable and matched from there.
2. **`strtonum` is a gawk extension.** The PT_LOAD alignment check used it;
   Debian's `awk` is mawk. The hex conversion moved into the shell.

Both are the same shape as the pattern the upstream record keeps flagging:
*make the check print what it measured, not a verdict.*

---

## Open, for when the console is involved

- Nothing here has run on hardware. `gate=pass` means the bundle **builds**, not
  that it runs; the bundle's own README is explicit about that.
- `config.env` needs the real console IP. GoldHEN's FTP is usually 2121, but
  stock FTP payloads often use 1337 — check which one is listening.
- Start `scripts/logs.sh` before launching anything. There is no usable stdout
  on this platform (`freopen` fails), so a black screen with no listener is a
  wasted evening.

---

## 2026-10-06 — `RADV_DEBUG=info` cannot be set on this console, and the way round it

Stage 2 was written assuming we would capture `radeon_info` the way the upstream
record does: `RADV_DEBUG=info`, then `sed` the `radv-info|` lines out of the UDP
capture. Neither half of that works from a stock bundle.

### The dump has nowhere to go

`radeon_info` does not go through `mesa_log`, and on this platform it cannot go
through stdout either — `freopen` fails. Upstream's harness patches the one call
site to `fopen` a file directly. We have Mesa as **prebuilt static archives**, so
there is no call site to patch without rebuilding Mesa.

### ⚠ And the switch cannot be thrown at all

This is the more interesting half, and it is the exact failure
`orbis-compat/scripts/check-env-knobs.sh` exists to catch — *"a switch that
cannot be thrown looks exactly like a switch with no effect."*

`orbis-compat/src/orbis_env.cpp` reads `/data/orbis-env.txt`, but `load_file()`
calls `store()`, which writes into a **private array** (`g_entry[]`). It does not
`setenv()`. Only code calling `orbis_env_get()` ever sees those values.

Mesa reads its knobs with plain `getenv()` — `RADV_DEBUG`, and the PS4 port's own
`ORBIS_TESS_WORKGROUPS` and `ORBIS_TF_BASE_BIAS`. On a console `getenv` returns
NULL for all of them, the code takes its default, and **the run comes back looking
like a measurement of the default**. Stage 3's entire tuning method depends on
those two knobs, so this would have invalidated it silently.

The fix is small, and it works *here* for a reason worth stating: RADV is
statically linked into our eboot, so Mesa and our `main()` share one `environ`.
`setenv()` early in `main()`, before `vkCreateInstance`, and Mesa sees it.

That is also why it failed upstream for RetroArch: a `.prx` core links its own
copy of static musl with its own `environ`, and the frontend's `setenv` never
reached it. Measured upstream 2026-08-23 — `ORBIS_NCPU=1` was applied to the
eboot's environ and the core still started 5 workers.

`src/probe/probe.c` does the `setenv` pass and logs each setting it applied, so
the knob being thrown is visible in the capture rather than assumed.

### What replaces the radeon_info diff

`VK_AMD_shader_core_properties` is present in this bundle's RADV (confirmed by
`strings` on `libvulkan_radeon.a`) and carries the fields stage 3 actually
depends on:

| Vulkan field | radeon_info field | Liverpool |
|---|---|---|
| `shaderEngineCount` | `max_se` | 2 |
| `shaderArraysPerEngineCount` | `max_sa_per_se` | 1 |
| `computeUnitsPerShaderArray` | `num_cu_per_sh` | 9 |
| product of the three | `num_cu` | 18 |

Plus `VkPhysicalDeviceProperties.deviceID`, which is `0x9924` for `CHIP_GLADIUS`
in `ac_gpu_info.c` — the direct answer to whether the Pro branch fired — and
`deviceName`, which RADV fills from `ac_get_marketing_name`.

That is most of what the `radeon_info` diff was going to tell us, through a
standard extension, with no Mesa rebuild. It is not all 164 fields. If the full
dump turns out to be needed, the path is to rebuild Mesa from the `orbis` branch
with the log call site patched, which is a much larger job than this stage needs.
