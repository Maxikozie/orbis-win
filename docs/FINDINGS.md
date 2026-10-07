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

---

## 2026-10-06 — first contact with the console

Preflight passed cleanly against `10.168.2.52`:

```
  console                10.168.2.52 up
  this host              10.168.2.47 via eth0
  ftp 2121               220 GoldHEN FTP server v2.2
  ftp 1337               no answer
  ftp 21                 no answer
  klog 3232              open
  udp 18194              free
```

The upload then failed on all three packages, and by the time it was traced both
GoldHEN services — FTP **and** klog — had stopped listening while the console
still answered ping. A TCP probe does not take down klog, so this reads as
GoldHEN unloading rather than anything the probe did. Nothing was transferred.

**Unresolved.** If it recurs, the thing to establish is whether GoldHEN survives
between preflight and upload at all, and the cheap way to find that out is to
re-run preflight immediately after a failed deploy rather than minutes later.

### Two real bugs this shook out

**`preflight.sh` read 96 bytes where it should have read one line.** An FTP
server sends its greeting and then holds the connection open waiting for
commands, so `head -c 96` blocked until `timeout` killed it — exit 124, and
under `set -e` that took the whole script down after printing two lines. The
first version was tested against a fake server that closed immediately, so the
test never modelled the behaviour that matters. Now it reads a single line with
`read -t`, and the replacement was checked against a responder that *holds* the
socket open.

**`deploy.sh` was being handed a symlink and sizing that.** `make-pkg.sh` leaves
`<TITLE_ID>.pkg` pointing at the real `IV0000-…pkg`, and `deploy.sh` sizes its
argument with `stat -c%s`, which reports **40 bytes** for the link:

```
== 40 B  .../pkgout/triangle/GLDS10001.pkg
```

deploy.sh's entire verification is *compare what arrived against what was sent* —
written that way because this project has repeatedly had status codes report
success over failure. A 40-byte local size silently guts that check on the one
step it exists to protect. `build.sh` now resolves the link before handing it
over.

Neither bug was found by reasoning about the code. Both appeared the moment real
hardware was on the other end, which is the argument for stage 1 existing as its
own stage.

---

## 2026-10-06 — all three packages on the console

`/data/pkg/` now holds `hello.pkg` (6,619,136 B), `triangle.pkg` and `probe.pkg`
(27,394,048 B each), every one verified by reading the directory back rather
than by trusting the `226`.

### ⚠ lftp's login is what kills GoldHEN

Reported by the console's owner, who watched it happen: *"before you logged in
it worked, then once you logged in it died."* Twice, each costing a reboot.

`deploy.sh` passes no credentials, so lftp falls back to **anonymous**, and that
login takes down the whole GoldHEN payload — FTP and klog both — leaving a
console that still answers ping. I had been about to blame my own klog probe
instead, which does not fit the evidence at all: the owner's observation was
specific and the probe theory was a guess.

`scripts/ftp-put.py` replaces lftp on this path. It attempts **no login at all**
and sends only what STOR requires:

```
TYPE I  →  MKD  →  PASV  →  STOR
```

No FEAT, no SYST, no OPTS, no PWD, no EPSV, no SIZE, no MLSD. Every command and
reply is printed before it is sent, so a server that dies again names the
command that did it. Three uploads and a directory listing since, with the
server alive throughout.

### `/data/pkg/` does not exist on a fresh console

The bundle's `deploy.sh` says *"/data/pkg AND NOWHERE ELSE"* and assumes the
directory is there. On this console it was not, and STOR answered:

```
550 Requested action not taken. File unavailable.
```

`/data` is `drwxrwxrwx`, so `MKD /data/pkg` is enough. ftp-put.py now creates the
parent and ignores the reply — an existing directory answers 550 here too, so
checking it would be worse than not.

### ⚠ These examples log to KLOG, not to the UDP netlog

Both `triangle.c` and `probe.c` write through `sceKernelDebugOutText`, which is
GoldHEN's **klog** channel on TCP 3232. Neither calls `orbis_netlog`. So
`scripts/logs.sh` with no argument — the UDP path, and the one the plan kept
calling primary — captures nothing from either of them.

**`scripts/logs.sh klog` is the one to run**, and it must be left draining: the
console's stdout write *blocks the calling thread* when nothing is reading that
socket. That is also the reason preflight's connect-and-close klog check was a
bad idea regardless of whether it caused anything, and it has been removed.

---

## 2026-10-06 — `hello` crashed before `main()`: empty `sce_module/`

First launch on hardware. The klog named it exactly, which is the entire reason
for running `hello` before anything that touches Mesa:

```
# A user thread receives a fatal signal
# exception: 0xa0020102 (PRX_SCE_MODULE_LOAD_ERROR)
# thread name: eboot.bin
...
# === Lack of a .prx file in /app0/sce_module is detected!!! ===
# Copy the file (e.g. libc.prx) from target/sce_module.
```

Not a crash in our code — the process never reached `main`. `libkernel.sprx`,
`libSceLibcInternal.sprx`, `libSceSysmodule.sprx` and `libSceDiscMap.sprx` all
mapped; the loader then refused for want of `/app0/sce_module`.

### The package declared the directory and shipped nothing in it

`make-pkg.sh`'s gp4 carries `<dir targ_name="sce_module" />` under `rootdir`,
and its `<files>` list had only `eboot.bin` and `sce_sys/param.sfo`. The
OELF's own needs are modest — `readoelf -d` shows one `SCE_NEEDED_MODULE`,
`libkernel` — so the requirement comes from the loader's rules for an app
process, not from our link line.

### Where the modules come from

`libc.prx` and `libSceFios2.prx` are **Sony firmware modules**. The OpenOrbis
SDK ships no `.prx` at all, for the obvious reason, and neither does this
repository — `sce_module/` is gitignored and `sce_module/README.md` documents
extracting them from your own console.

Any installed OpenOrbis homebrew carries both. Here they came out of Apollo Save
Tool (`APOL00004`): `RETR /user/app/APOL00004/app.pkg` over the same FTP path,
then `PkgTool.Core pkg_extract`, and they land under `uroot/sce_module/`.

```
8ac205d14351edb241238b372b2f1c3350a6385a271483d4867fe9a33bc6e4d3  libc.prx          3424 B
905cf6b7c699eccb6b95291fc4dcaf5ce2d2605f450f36496d9b973777afa745  libSceFios2.prx   3200 B
```

Both are a few kilobytes — stubs that satisfy the loader, not implementations.
The real code is in `libSceLibcInternal.sprx`, which the process already maps.

⚠ `PkgTool.Core pkg_extract` needs the same OpenSSL 1.x shim and
`DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` that `make-pkg.sh` sets up, and
without them it prints `No usable version of libssl was found` **and exits 0** —
a failure that reads as a successful run with no output.

`build.sh` now refuses to package at all if either module is missing, rather
than building something that cannot boot.

Packages rebuilt and re-uploaded: triangle and probe grew 27,394,048 → 27,525,120 B.

---

## 2026-10-06 — RADV RENDERS ON THE PRO. And the Pro is running as a base PS4.

Stage 1 is done. `hello` prints and `triangle` draws:

```
orbis-sdk bundle: hello
orbis-sdk bundle: all checks passed

orbis-sdk bundle: triangle
triangle: ok   vkCreateInstance
triangle: ok   vkCreateHeadlessSurfaceEXT
triangle: GPU 'PlayStation 4 Liverpool (RADV ORBIS) (RADV LIVERPOOL)', api 1.3.359, driver 0x06802063
triangle: queue family 0 renders and presents
triangle: ok   vkCreateDevice
triangle: surface 1920x1080, format 37, 4..0 images
triangle: ok   vkCreateSwapchainKHR    ... swapchain has 4 images
triangle: ok   vkCreateGraphicsPipelines
triangle: setup complete - presenting
triangle: frame 1024 presented
```

Every call checked and named, a pipeline compiled from SPIR-V, 1024+ frames at
1920x1080. **`api 1.3.359` — Vulkan 1.3**, not the 1.1 the gfx7 framing led me
to expect.

⚠ `hello`'s black screen is CORRECT and I should have said so in advance. Both
examples end in `for (;;) sleep(1)` deliberately: returning from `main()` on this
console is reported as CE-34878-0 and reads exactly like a crash. `hello` draws
nothing, so black is the right picture.

### The probe's answer: 18 compute units, device id 0x9920

```
probe: 197 device extensions, VK_AMD_shader_core_properties present
deviceName        PlayStation 4 Liverpool (RADV ORBIS) (RADV LIVERPOOL)
vendorID          0x1002
deviceID          0x9920
shaderEngineCount        2      <- max_se
shaderArraysPerEngine    1      <- max_sa_per_se
computeUnitsPerShaderArr 9      <- num_cu_per_sh
=> compute units         18
simdPerComputeUnit       4   wavefrontsPerSimd 10   wavefrontSize 64
heap 0  661 MB host      heap 1  1322 MB device-local
family 0  1 queue(s) graphics compute transfer
```

`ac_gpu_info.c` maps `0x9920/0x9922/0x9923 -> CHIP_LIVERPOOL` and `0x9924 ->
CHIP_GLADIUS`. The console answered **0x9920**, and the topology is exactly the
Liverpool baseline — 2 SE, 1 SA, 9 CU/SA.

**This is not a driver bug. The title was never given the Pro.**

### ⚠ Root cause: param.sfo ATTRIBUTE bit 24

A PS4 Pro runs a title in base-PS4 compatibility mode unless the title declares
NEO support: `param.sfo` `ATTRIBUTE` flag 24, `0x00800000` (8388608). In base
mode the extra compute array is simply not there to be enumerated — the GPU
answers Liverpool's device id and reports 18 CUs, which is precisely what we
measured.

`make-pkg.sh` writes `sfo_set ATTRIBUTE Integer 4 0`. Every package this project
has built so far has been a base-PS4 package.

`build.sh --neo` now sets it. make-pkg.sh takes no `--attribute`, so the staged
SFO is edited afterwards and the package rebuilt from the gp4 make-pkg.sh
already wrote — reusing its file list rather than composing a second one.

Verified by parsing the SFO out of the uploaded package's bytes rather than
trusting the tool, because `PkgTool.Core` has already been seen to fail and exit
0 once this session:

```
param.sfo magic at 0x3db0
  APP_TYPE     = 0x1
  ATTRIBUTE    = 0x800000
  TITLE_ID     = GLDS10002
```

⚠ `pkg_extract` will not show you this: it unpacks the inner PFS (`uroot/`)
only, and `param.sfo` is a PKG entry rather than a PFS file.

The SDK also exposes `sceKernelHasNeoMode()` (is the CONSOLE a Pro) and
`sceKernelIsNeoMode()` (did THIS TITLE get Neo mode). probe.c now prints both,
so the next run distinguishes "package was wrong" from anything else.

### Still unknown

Whether the attribute alone is sufficient. It may also need the eboot built with
a Neo flag, or an explicit `sceKernelSwitchToNeoMode()` call. The two
`sceKernel*NeoMode` readings will say which.

### Logging note

GoldHEN's klog server **replays its buffer on connect**, so reconnecting after a
run still yields the whole run. That is how the triangle output above was
recovered after the receiver had gone stale.

And the receiver does go stale: the console suspended at 19:28:48
(`State Change: SUSPEND_ON_GOING`, `HW Indicator: Orange Blink`) and the socket
stayed ESTAB while delivering nothing. Check that the log is still growing, not
that the connection still exists.

---

## 2026-10-06 — THE PS4 PRO'S OTHER 18 COMPUTE UNITS WERE BEING MASKED BY THE DRIVER

`ac_orbis_drm.c` hardcoded `num_shader_engines = 2` and `device_id = 0x9920`,
both marked UNCITED. `cu_bitmap` is built from that, and RADV programs
`COMPUTE_STATIC_THREAD_MGMT` from `cu_bitmap` — so the driver was not merely
*mis-reporting* a Pro as a base PS4, it was configuring it as one.

Patched to pick Gladius from `sceKernelIsNeoMode()` (`patches/0001-*.patch`),
rebuilt Mesa, relinked, measured. Same console, same binary, same benchmark —
only the driver's idea of the topology changed.

### The scaling knee doubled

Per-wavefront rate while every wave still has a SIMD to itself: **6.125 GFLOP/s**.
`%lin` is the measured throughput against that straight line.

| waves | 18-CU config | %lin | 36-CU config | %lin |
|---|---|---|---|---|
| 8 | 48.9 | 100% | 49.0 | 100% |
| 16 | 97.5 | 99% | 97.5 | 99% |
| 32 | 195.8 | 100% | 194.0 | 99% |
| 64 | 384.3 | 98% | 384.7 | 98% |
| **96** | 464.6 | **79%** | 571.2 | **97%** |
| **128** | 623.8 | **80%** | 769.0 | **98%** |
| 192 | 689.4 | 59% | 908.5 | 77% |
| 256 | 739.1 | 47% | 1250.3 | 80% |

Before: linear to 64, broken by 96 → the machine has between 64 and 96 SIMDs.
**72** (18 CU × 4) is in that bracket; 144 is not.

After: linear all the way to 128, broken by 192 → between 128 and 192 SIMDs.
**144** (36 CU × 4) is in that bracket; 72 is nowhere near it.

The knee is the measurement that does not care about clock or ALU efficiency —
those scale the whole curve and leave the knee where it is.

### The plateau and the efficiency agree with it

```
plateau   950 -> 1830 GFLOP/s   (1.93x)

  18 CU @ 800 MHz: theoretical 1843, measured  950  -> 51.5% efficiency
  36 CU @ 800 MHz: theoretical 3686, measured 1830  -> 49.6% efficiency
```

Essentially **identical efficiency on twice the hardware**, which is what
doubling a compute array looks like and is not what a mislabelled constant looks
like. It also pins the GPU clock at ~800 MHz rather than the Pro's nominal 911 —
at 911 the efficiency would have to be 44%, and it would have had to *drop* on
the larger configuration for no reason.

### Attribution is clean

`/data/orbis-env.txt` was deleted before this run and the probe confirms it:

```
probe: sceKernelHasNeoMode()=1  sceKernelIsNeoMode()=1
probe: getenv(ORBIS_NUM_SE) = (null)
probe: getenv(ORBIS_CHIP)   = (null)
deviceID          0x9924   <- CHIP_GLADIUS
shaderEngineCount 4
=> compute units  36
```

So the four engines came from Neo detection inside the driver, not from a
leftover knob.

### The inconsistent raster_config does not break basic rasterisation

`ac_gpu_info.c`'s `CHIP_GLADIUS` entry pairs `0x2a00161a` (a TWO-engine high
half) with `0x0000002e` (Hawaii's FOUR-engine `SE_PAIR_MAP`), and its comment
calls the pair internally inconsistent and unresolvable without a Pro. The
triangle renders correctly on screen with it.

⚠ That is a weak test and should not be read as more. One full-screen triangle
barely exercises work distribution across shader engines, which is what that
register governs; upstream describes the failure mode as "quarter-of-the-screen
shaped". It rules out catastrophic breakage, not misbinning.

### Loose end, honestly unresolved

Setting `ORBIS_NUM_SE=4` through `/data/orbis-env.txt` earlier did **not** change
`shaderEngineCount`, even though the string is present in the bundled driver and
`ac_gpu_info.c:1249` assigns `max_se` from it with no clamp. The `getenv`
read-back that would have separated "our setenv never reached Mesa" from
"something downstream overrode it" was added to the probe but only ever ran with
the file absent, so it answered `(null)` and settled nothing. Moot for the fix —
the driver now decides for itself — but not explained.

---

## 2026-10-07 — stage 5 opens: the translator cross-compiles for the PS4 unmodified

Two console-free checks, both green.

### The upstream host suite passes here

`make test` in a fresh `prospero-win` checkout: **exit 0**, after installing
`gcc-mingw-w64-i686` and `gcc-mingw-w64-x86-64` (one test hard-asserts on them;
the rest of the Wine group skips cleanly without a host Wine). The line that
matters:

```
dynarec benchmark regression passed: 5 deterministic workloads; reg_alu matches native i386
```

So the translator is correct on this machine before anything PS4-specific
touches it. That is the baseline any later breakage gets measured against.

### And it builds for Orbis with no edits

`src/dbt/CMakeLists.txt` globs `prospero-win/src/*.c` through the bundle's
toolchain file: **24 translation units, 0 errors, 0 warnings under
`-Wall -Wextra`**, producing a 542 KB `libpw_dbt.a` for `elf64-x86-64`.

The undefined set is 39 symbols and contains nothing surprising:

```
memcpy memmove memset memchr bcmp      <string.h>
strchr strcmp strlen strncmp
mmap mprotect munmap                   the POSIX VM backend (pw_vm_posix.c)
orbis_sysconf                          orbis-compat interposing sysconf
__udivti3 __umodti3                    compiler-rt, 128-bit divide
pw_*                                   internal, resolved in-archive
```

No allocator, no formatter — which is upstream's deliberate choice, made so a
PS5 firmware's wrong libc symbols could not be inherited, and it is precisely
what makes the PS4 target cheap. `ARCHITECTURE.md` says so outright: *"the
cheapest way not to inherit that class of bug is not to import the symbol."*

⚠ **This is a build, not a run.** It says the headers and the ABI agree. Whether
translated code is correct on Jaguar is a hardware question; the host suite
answers the correctness half and the console has not seen any of this yet.

### What this does to the stage 5 estimate

The 13,519-LOC translator was the piece I could not cost without trying it. It
needed nothing. What remains is unchanged: ~10,700 LOC of platform layer
(`native/`, `wine/ps5/`, `wine/wineps5/`) to rewrite against PS4 APIs, and 81
Wine patches whose constants are *measured* rather than ported and must be
re-measured on Orbis.

---

## 2026-10-07 — the first prospero-win frame on a PlayStation 4

`Prospero Presenter` (GLDS10003) draws three colour bars through the
**unmodified** portable compositor and flips them: white bar on top, RED GREEN
BLUE left to right, 600 flips, steady.

```
present: 25165824 bytes mapped, 3 buffers of 8388608
present: opened with user 0xff
present: pixel format 0x80000000 A8R8G8B8 - byte0=B, matches the compositor (register rc 0)
present: compose -> 0, placed 1920x1080 at 0,0 scale 4
present: 600 frames, 600 flips
```

### The port is 170 lines, against ~350 on the PS5

| PS5 | PS4 |
|---|---|
| tiled intermediate + `pw_videoout_tile.h` index tables | `ORBIS_VIDEO_OUT_TILING_MODE_LINEAR` — gone |
| `pw_agc_ps5.c`: DMA the tiled frame into scanout, `sceAgcDcbSetFlip` | `sceVideoOutSubmitFlip` — gone |
| one copy per frame | none: the compositor draws straight into scanout |

`pw_present.h`'s target is already *"a writable linear BGRX surface"*, which is
exactly what a linear VideoOut buffer is. The PS5 needed the tiling pass and the
DMA because its scanout is tiled; the PS4's is not, so the whole intermediate
disappears.

### Two bugs, both found by running it, both measured rather than reasoned

**The pixel format was reversed.** With `A8B8G8R8_SRGB` (the only format the SDK
`#define`s) a red source column reached memory as `00 00 ff ff` — *correct*
B,G,R,A from the compositor — and the television showed it **blue**. So VideoOut
reads byte 0 as red: the order it wants is R,G,B,A, exactly as the name says
read MSB-first.

`pw_present.c`'s `store_opaque` writes B,G,R,A and is portable code we do not
change, so the fix is the format whose byte 0 is blue: **`A8R8G8B8`,
`0x80000000`**. Not in the SDK headers; named in `pw_videoout_ps4.c` with the
measurement beside it. `open()` tries it first and falls back to the defined
one, recording which was accepted — so a wrong guess would cost reversed colours
rather than a black screen. It was accepted first try.

⚠ My diagnostic *text* had the interpretation backwards on the first run. The
numbers were right and the conclusion from them was wrong by one inversion,
which is worth remembering: a dump is only as good as the sentence next to it.

**Two buffers flickered.** "Flip, then wait one vblank" is not enough — a CPU
compose of 1920x1080x4 does not reliably finish inside a frame, so the flip
lands late and the next compose starts writing the buffer still being scanned.
**A vblank is not the same event as a flip retiring.**

Three buffers now, and `acquire()` waits on `numFlipPending` from
`sceVideoOutGetFlipStatus` — the actual question — rather than on a vblank. The
wait lives in `acquire()` rather than `present()`, because blocking after a flip
stalls a caller that already has a free buffer to draw into.

### Two things the run corrected about my own expectations

- 480x270 at 4x is **exactly** 1920x1080, so there is no surround; the
  "surround pixel" check was reading the white bar. The composition was right
  and the expectation was wrong.
- `sceVideoOutOpen` with `0xFF` worked, so the signed-in-user fallback never
  fired. Kept anyway — it costs one call and removes a failure mode that has no
  other symptom.

### What this establishes for stage 5

The platform boundary is real now, not theoretical. The portable half of
prospero-win (`src/`, 13.5k LOC) builds for the PS4 unmodified and its output
reaches the screen through a PS4-native presenter. `native/pw_videoout_ps5.c`
and `pw_agc_ps5.c` are the first two of the ~10.7k LOC platform layer to be
replaced, and they came to 170 lines.

---

## 2026-10-07 — the DualShock 4 drives prospero-win's pad core

```
pad: open, user 0x173c98d4 handle 50922752
pad: pressed 0x00004000 held 0x00004000 L(121,119) R(125,122) L2=  0 R2=  0
pad: pressed 0x00000200 held 0x00000200 L(121,116) R(125,122) L2=  0 R2=198
pad: 3600 polls, 30587 connected, 732 empty, 0 errors, 1 generations
```

CROSS red, CIRCLE green, TRIANGLE yellow, SQUARE blue, L2/R2 dim — all four
face buttons and both analogue triggers, confirmed on screen.

### The core is upstream's, compiled unmodified

`native/pw_pad_ps5.c` reaches the platform only through `PwPadPs5Ops`, and
already guards its own PS5 filling of that vtable behind
`PW_PAD_PS5_HOST_TEST`. Defining it keeps the batching, connect/disconnect
generations, intercepted samples, edge detection and analogue centring, and
drops the eight PS5 forwards. `src/native/pw_pad_ps4.c` supplies those — 96
lines, of which seven functions are one line because **the PS4 exports the same
eight symbols**.

The ninth is the only real work. `OrbisPadData` and the DualSense sample are not
binary compatible: `connected` is `uint8_t` here and `int32_t` there, which
moves every field after it, and the accelerometer and gyro are swapped. So the
read lands in the PS4's struct and the seven fields `read_batch()` consumes are
copied by name. Everything else in `PwPadPs5Data` stays zeroed rather than
inventing a DualSense this console does not have. Button bits need no
translation — both consoles use Sony's layout.

### What the numbers prove

| reading | what it establishes |
|---|---|
| `pressed 0x4000` etc. | edge detection works through a vtable it was never written for |
| `L(121,117) R(125,121)` at rest | the struct translation landed on the right offsets — near 128 centre with a used pad's drift |
| `L2=50 R2=198` | the analogue path survives the copy |
| `0 errors, 1 generations` | it never once thought the controller had disconnected |
| 30587 samples / 3600 polls | ~8.5 per read: upstream's batching, running |

### Two bugs on the way, both mine, neither the PS4's

**A NULL key map silently disabled the pad.** `pw_pad_init` refuses one outright
(`if(!pad || !map || !count ...) return PW_ERR_PRECONDITION`), so
`pw_pad_ps5_open` returned before touching a single platform call. The failure
message then printed `pad_init_rc` and `user_initialize_rc` — both still zero
from the memset — which read exactly like the platform layer had succeeded.

⚠ **A misleading diagnostic is worse than none.** The fix was not only the map
but printing every step, so the failing one names itself instead of being
inferred from two zeroes.

**The surround kept being invisible** because 480x270 (4x) and 320x180 (6x) both
fill 1920x1080 *exactly*, leaving no border for the pad to tint — which looked
like the pad was broken when the pad was fine. 300x170 scales to 1800x1020 at
60,30 and leaves one. `surround px[0] = 20 18 18 ff` now matches the expected
value exactly, where before it was reading the white bar.

### Stage 5 platform layer so far

| upstream file | lines | replaced by | lines |
|---|---|---|---|
| `pw_videoout_ps5.c` + `pw_videoout_tile.h` + `pw_agc_ps5.c` | ~350 | `pw_videoout_ps4.c` | 170 |
| `pw_pad_ps5.c`'s platform block | ~15 | `pw_pad_ps4.c` | 96 |

The pad core itself, and the whole of `src/`, are upstream code compiled as-is.

---

## 2026-10-07 — audio, and the pattern that has held for three pieces

```
audio: port 536870919 open, 256 frames per grain at 48000 Hz, S16 stereo
audio: 187 grains played - a 440 Hz tone, about a second
```

187 x 256 = 47,872 frames at 48 kHz, 0.997 s, no failed output. Audible on the
television.

`src/native/pw_audio_ps4.c` is **62 lines**, all forwards. The constants are
identical on both consoles — `PORT_MAIN` 0, `FORMAT_S16_STEREO` 1, the `0xff`
system user, `0x8000` for 0 dB — so nothing translates. Worth checking rather
than assuming: upstream names them privately instead of importing them, and a
wrong format constant produces **silence rather than an error**, which is the
hardest kind of wrong to notice.

⚠ The tone plays *before* the video loop, not interleaved. `sceAudioOutOutput`
blocks until the port takes the grain, and 256 frames at 48 kHz is 5.33 ms
against a 16.6 ms frame: one grain per frame starves the port, three couples the
render loop to the audio clock. Wine's driver hands down one grain at a time and
clocks itself, so the real integration will not have this shape — but a test
whose job is "does the port open and play" should not inherit a timing problem
it is not testing.

## The platform layer so far

| upstream | lines | this port | lines | upstream code reused |
|---|---|---|---|---|
| `pw_videoout_ps5.c` + `pw_videoout_tile.h` + `pw_agc_ps5.c` | ~350 | `pw_videoout_ps4.c` | 170 | the compositor, unmodified |
| `pw_pad_ps5.c` platform block | ~15 | `pw_pad_ps4.c` | 96 | the whole pad core, unmodified |
| `pw_audio_ps5.c` platform block | ~20 | `pw_audio_ps4.c` | 62 | `pw_audio_ps5_open_port`, unmodified |
| `src/` — the i386 translator | — | — | 0 | 13,519 LOC, unmodified |

**The pattern, three for three:** upstream separated portable code from platform
code with a vtable and a `*_HOST_TEST` guard, built for host testing, and that
seam serves a different console just as well. Two of the three ports are forwards
with one translating function between them; the presenter is larger only because
the PS4 needs *less* than the PS5 and the difference had to be written down.

⚠ **This does not generalise to what is left.** `wine/ps5/` is a PRX loader, a
heap, threads and a working-directory shim — none of it sits behind a vtable,
and the 81 Wine patches carry constants that were *measured* on PS5 firmware
rather than ported. The easy three were easy because somebody designed them that
way.

---

## 2026-10-07 — the module loader ports too, and the PS5 offsets are right on this console

```
dl: sceKernelGetModuleInfo(0) -> 0
dl: pw_prx_parse_module_info -> 0, name 'eboot.bin', 3 segment(s)
dl:   seg 0  0x400000  589824 bytes  prot 0x5
dl:   seg 1  0x49c000  688128 bytes  prot 0x3
dl:   seg 2  0x490000   49152 bytes  prot 0x1
```

I expected `wine/ps5/` to break the pattern. It mostly does not:

- `pw_wine_prx.c` uses **no Sony APIs at all** — pure PRXDESC1 parsing, which
  upstream calls host-testable and means it.
- `pw_wine_dl.c` reaches the kernel only through `PwWineDlOps`: *"The kernel
  calls are injected so the logic is host-testable."*
- All three calls exist on the PS4 — `sceKernelLoadStartModule`,
  `sceKernelGetModuleInfo`, `sceKernelStopUnloadModule` — and `sceKernelDlsym`
  besides.

`src/native/pw_wine_dl_ps4.c` is **73 lines of forwards**. No guard macro was
even needed: upstream keeps the Sony calls in a separate `*_libc.c` companion,
which this build simply does not compile.

### The layout claim, and why it was worth testing rather than computing

`pw_prx_parse_module_info()` hardcodes PS5 offsets. The PS4 SDK's
`OrbisKernelModuleInfo` computes to the same ones:

| field | computed from the PS4 header | parser expects |
|---|---|---|
| `name` | 0x0008 | 0x0008 |
| `segmentInfo[4]` | 0x0108 | 0x0108 |
| `segmentCount` | 0x0148 | 0x0148 |
| total | 0x0160 | 0x0160 |

But that is a claim about a **header**, and the kernel is the authority — so the
test asks the kernel about module 0 and hands the raw bytes to upstream's
parser. The result is not merely "no error":

```
text   0x400000 + 0x90000 = 0x490000   where rodata begins
rodata 0x490000 + 0x0C000 = 0x49C000   where data begins
```

**The segments tile exactly**, in the right order, with the right protections
(0x5 R|X, 0x1 R, 0x3 R|W), and 0x400000 matches the load address from this
morning's `PRX_SCE_MODULE_LOAD_ERROR` dump. Wrong offsets do not produce a
contiguous correctly-permissioned tiling by accident.

⚠ A wrong offset here would not have errored. It would have read segment
addresses from the wrong place, and the descriptor scan would have failed later
looking like a missing PRXDESC1 — several layers from the cause.

### Four for four

| upstream | lines | this port | lines | upstream reused |
|---|---|---|---|---|
| videoout + tiling + AGC | ~350 | `pw_videoout_ps4.c` | 170 | the compositor |
| pad platform block | ~15 | `pw_pad_ps4.c` | 96 | the whole pad core |
| audio platform block | ~20 | `pw_audio_ps4.c` | 62 | `pw_audio_ps5_open_port` |
| dl platform block (`*_libc.c`) | ~70 | `pw_wine_dl_ps4.c` | 73 | dlopen/dlsym/dladdr **and** all PRXDESC1 parsing |

⚠ **Still not generalising to everything.** This proves the *loader* ports. It
does not load a Wine PRX, because none is built yet — that needs
`tools/build_wine_ps5.sh`, the 81-patch series, and a working-directory, heap
and thread shim none of which sits behind a vtable. What it does establish is
that when a Wine PRX exists, the machinery to find and resolve it is in place
and its module-info reading is correct on this hardware.

---

## 2026-10-07 — direct memory, and five for five

```
dmem: add_region(0x40000000, 16777216) -> 0
dmem: commit 262144 bytes RW -> 0, backed 262144
dmem: wrote and verified 65536 words, 0 mismatched
dmem: fresh page reads 0x00000000 (expect 0)
dmem: backed 278528 peak 278528, 2 allocs 2 maps 0 protects 0 failures, 1 runs
dmem: replace(..,0) -> 0, backed now 0, 1 releases
```

The whole lifecycle: reserve an address region, commit pages into it, write a
pattern through those addresses and read it back, confirm a freshly backed page
arrives zeroed, then give it all back with nothing left behind.

⚠ **The read-back is the test, not the return codes.** `reserve`, `allocate` and
`map` all returning 0 says only that the kernel accepted the calls. A map at the
wrong physical offset returns success and hands back somebody else's page. 65,536
words written and verified through the mapped addresses is what proves the
memory landed where it was asked for.

### Two PS4-specific traps

**`sceKernelAllocateMainDirectMemory` is unusable here.** The OpenOrbis header
declares its last parameter `off_t` **by value** where it must be `off_t *` — the
physical offset cannot come back. A header bug, and one that would have produced
a garbage offset and a map landing nowhere. `sceKernelAllocateDirectMemory` has
the correct out-pointer and an explicit `[start, end)` search range, which the
PS5's "main" variant does not take.

**The memory type is not the scanout's.** `pw_videoout_ps4.c` uses WC_GARLIC (3)
because the compositor only ever writes a surface the display only ever reads.
Wine's heap and stack are read far more than written, so this uses WB_ONION (0),
cached and CPU-coherent. The wrong one here works and is slow in a way nothing
reports.

⚠ And a correction to the presenter while passing: its comment called type 3
*WB_ONION*. It is WC_GARLIC. Right value, backwards reason — left as a visible
note rather than quietly fixed, because a constant that works for a reason
nobody wrote down is a constant somebody later "fixes".

### A misread of the API that looked like a leak

The first run called `pw_wine_dmem_protect(range, 0)` to free, and got `rc 0`
with `backed_bytes` unchanged and `releases` still 0 — which reads exactly like
a leak in this port. It is not:

- `protect(range, 0)` declines to **back** reserved pages; backed pages stay.
- `replace(range, 0)` is the one whose header says *"protection 0 leaves it
  reserved"*, and which releases the direct memory behind it.

Worth recording because the failure mode is a leak that **reports success**.

### Five for five

| upstream | lines | this port | lines | upstream reused |
|---|---|---|---|---|
| videoout + tiling + AGC | ~350 | `pw_videoout_ps4.c` | 170 | the compositor |
| pad platform block | ~15 | `pw_pad_ps4.c` | 96 | the whole pad core |
| audio platform block | ~20 | `pw_audio_ps4.c` | 62 | `pw_audio_ps5_open_port` |
| dl platform block | ~70 | `pw_wine_dl_ps4.c` | 73 | dlopen/dlsym/dladdr, PRXDESC1 parsing |
| dmem platform block | ~130 | `pw_wine_dmem_ps4.c` | 121 | the whole region allocator |

**522 lines of PS4 platform code** against `src/`'s 13,519 LOC translator plus
the pad core, the compositor, the module loader and the region allocator, all
compiled unmodified.

⚠ The pattern is upstream's doing, not this port's cleverness: every one of
these five sits behind a vtable or a `*_HOST_TEST` guard that somebody put there
on purpose. What is left — the heap, threads, the working-directory shim, and
the 81 Wine patches whose constants were *measured* on PS5 firmware — does not.

---

## 2026-10-07 — the heap's reason for existing does not apply to the PS4

`pw_wine_heap.c` needed **no porting at all** — 265 lines of pthread, string.h
and sys/mman.h, no Sony calls anywhere, compiles for the PS4 as-is. Its
`*_libc.c` companion is deliberately not built: that one interposes malloc/free
globally, and the heap should be exercised through its own API before every
allocation in the process depends on it.

But it exists for a **measured PS5 reason**, stated in its own header: *"Heap for
Wine's Unix side on the PS5, where the title's libc malloc stops near 13 MiB."*

That is not true here.

```
heap: libc malloc reached 1024 MiB in 1048576-byte blocks (hit the test's ceiling)
heap: pw_wine_heap reached 433 MiB before refusing
heap: live 454033408 mapped 461127680 peak 461127680, 433 allocs 1 failures 433 large
heap: foreign free counted 1 times (expect 1), live now 0
heap: memalign(4096,100) -> 0x229c01000 aligned=1 usable=100
```

**PS5 libc malloc: ~13 MiB. PS4 libc malloc: over 1 GiB, never refused.** At
least 80x, and the real ceiling is unknown because the test stopped first.

And prospero's heap is the *more* constrained of the two on this console: 433
MiB, because it maps each large block separately and meets a mapping limit libc
does not.

⚠ **The first run's numbers were right and I distrusted them for a good reason.**
It touched one byte per 16 KiB, so if flexible memory paged at 4 KiB only a
quarter of each block would have been committed and libc's figure inflated by
exactly that. Re-run committing every 4 KiB: **both numbers identical**, 433 MiB
and the ceiling again. The hypothesis was worth testing and was wrong.

That 433 MiB repeating exactly across two runs is itself the signal — a pool
limit, not noise.

### What it means for the port

On the PS4, Wine's Unix side can likely use libc malloc directly and skip this
heap entirely: simpler, and more than twice the headroom. The heap becomes
insurance rather than necessity.

⚠ **Not yet a decision.** What is measured is *large-block* behaviour, 1 MiB at
a time. The PS5 cap may bite differently on the small-block path (the heap
carves power-of-two classes out of 1 MiB mappings for anything under 64 KiB),
and Wine allocates small far more often than large. Worth measuring before
anything is removed.

The rest of the heap checks out regardless: foreign frees counted and ignored
exactly once, `memalign(4096,100)` correctly aligned with usable size 100, live
bytes back to 0 after every free.

### Six for six

| upstream | lines | this port | lines |
|---|---|---|---|
| videoout + tiling + AGC | ~350 | `pw_videoout_ps4.c` | 170 |
| pad platform block | ~15 | `pw_pad_ps4.c` | 96 |
| audio platform block | ~20 | `pw_audio_ps4.c` | 62 |
| dl platform block | ~70 | `pw_wine_dl_ps4.c` | 73 |
| dmem platform block | ~130 | `pw_wine_dmem_ps4.c` | 121 |
| **heap** | — | **nothing** | **0** |
