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

---

## 2026-10-07 — threads, and a scheduling fact no header states

`pw_wine_threads.c` needed no porting: 62 lines of pthread, errno, sched,
signal and unistd. Its companion did — `pw_wine_threads_libc.c` includes
`<sys/thr.h>` and **the PS4 SDK has no such header**. Replaced in eight lines.

```
threads: register(0x7357) -> 0
threads: kill(unregistered) -> -1 errno 3 (ESRCH)
threads: kill(registered) -> 0, handler ran = 1
threads: kill after unregister -> -1
```

The signal reached the specific registered thread, an unregistered id was
refused rather than guessed at, and unregistering took effect — which matters
because the kernel reuses thread ids, and a stale entry would deliver Wine's
suspend signal to the wrong thread.

### ⚠ thr_self is not a function on the PS4

The SDK knows it only as syscall number 432 (`SYS_thr_self`); libkernel exports
no `thr_self`, `thr_kill` or `thr_kill2`. Which is exactly why this registry
exists — wineserver signals with `thr_kill2(pid, lwpid, sig)`, no console
provides it, so ntdll maps kernel ids onto `pthread_t` and `thr_kill2` becomes a
lookup plus `pthread_kill`.

**Open, deliberately not guessed at:** which `long` ntdll will register as the id
here. musl's `syscall()` is on the link line but uses Linux conventions, and
whether `SYS_thr_self` survives that on a FreeBSD-derived kernel is untested. It
does not matter yet — the registry is a map and any unique key works, which is
why the test uses synthetic ids — but ntdll and wineserver must agree once both
are real.

### Orbis admits only the realtime policies, and the range is 256..767

`pthread_setschedparam(SCHED_OTHER, 0)` returned **EINVAL**. Probed on hardware
rather than reasoned about:

| policy | `sched_get_priority_min` | `max` | `pthread_setschedparam` |
|---|---|---|---|
| 0 — musl's `SCHED_OTHER` | -1 | -1 | **22 EINVAL** |
| 1 — `SCHED_FIFO` | 256 | 767 | **0** |
| 2 — musl's `SCHED_RR`, FreeBSD's `SCHED_OTHER` | -1 | -1 | **22 EINVAL** |
| 3 — FreeBSD's `SCHED_RR` | 256 | 767 | **0** |

⚠ **My hypothesis was half right, and the half that was wrong is the
interesting half.** The SDK's `sched.h` is musl's and carries Linux policy
numbers (`OTHER 0, FIFO 1, RR 2`) while this kernel is FreeBSD-derived
(`FIFO 1, OTHER 2, RR 3`) — so I expected policy 2 to be accepted as FreeBSD's
`SCHED_OTHER`. It is not. **Orbis admits only 1 and 3, the two realtime
policies, and rejects `SCHED_OTHER` under either numbering.** That is a console
deciding something, not a header disagreeing.

Consequences for anything using this SDK:

- `SCHED_OTHER` from `sched.h` is **never** valid. Code that passes it gets
  EINVAL, and the header gives no hint.
- `SCHED_RR` from `sched.h` is **2**, which is also rejected. The accepted RR is
  **3**.
- `SCHED_FIFO` is the one value where musl and this kernel agree.
- The priority range is **256..767**, stated in no SDK header. `sched_get_
  priority_min/max` is the only source, and it returns -1 for a policy the
  kernel does not take — which is a usable probe.

This is the class of defect `orbis-compat` exists to correct, and it is
uncorrected. Worth sending upstream with the Gladius measurement.

### Seven for seven

| upstream | lines | this port | lines |
|---|---|---|---|
| videoout + tiling + AGC | ~350 | `pw_videoout_ps4.c` | 170 |
| pad platform block | ~15 | `pw_pad_ps4.c` | 96 |
| audio platform block | ~20 | `pw_audio_ps4.c` | 62 |
| dl platform block | ~70 | `pw_wine_dl_ps4.c` | 73 |
| dmem platform block | ~130 | `pw_wine_dmem_ps4.c` | 121 |
| heap | — | nothing | 0 |
| threads companion | ~7 | `pw_wine_threads_ps4.c` | 8 |

---

## 2026-10-07 — the working directory, and a lesson about instrumentation

Both halves of `pw_wine_cwd` are pure POSIX, so neither needed porting. What
they needed was the **28 `--wrap` flags**: the libc half defines `__wrap_open`
and calls `__real_open`, and lld only creates that pairing when told to, per
symbol. A build missing them links cleanly, runs, and resolves no relative path
at all — the failure is **silence**.

### The kernel, measured through `__real_*`

```
__real_getcwd          -> (null) errno 78   ENOSYS
__real_chdir("/data")  -> -1     errno 1    EPERM
__real_open(relative)  -> -1     errno 2    ENOENT
__real_symlink         -> -1     errno 78   ENOSYS
```

| | PS5 (upstream's measurement) | PS4 (this console) |
|---|---|---|
| `chdir` | EPERM for every path | **EPERM** — same |
| `getcwd` | **faults** | ENOSYS |
| `symlink` | **kills the process** | ENOSYS |

So the problem is real here and the shim is needed — but upstream's two
*dangers* are not. A stray `symlink` merely fails; it does not take the title
down. That makes this code considerably safer to develop against than its own
header warns.

### The shim, driven

```
pw_cwd_set(/data) -> 0, pw_cwd_get -> '/data'
open("pkg") relative   -> 15        resolved against /data
set("..")              -> -1 EINVAL correctly refused
set(/data/pkg/..)      -> '/data'   folded lexically
stat("pkg") relative   -> 0, dir=1  stat is wrapped too, not just open
```

That last line matters: Wine touches all 28 of these, and one unwrapped call is
a path silently resolving against nothing.

### ⚠ Twice in this piece my instrumentation was the thing at fault

**A run reported that `chdir` and `symlink` "work, unlike the PS5".** They do
not. Once the wraps are on the link line those names resolve to the shim's own
logical implementations — the test was measuring this port and calling it the
console. Fixed by probing `__real_*` and the wrapped names side by side in one
run, so neither answer can be mistaken for the other.

**A run reported that lexical folding had failed.** It had not: `pw_cwd_set`
documents `EINVAL` for a relative path, and `".."` was correctly refused.
Folding applies to `".."` *inside* an absolute path. The test was misusing the
API.

Both would have entered this record as console facts. A test that reports
confidently and wrongly is worse than no test, and the only thing that caught
either was going back over numbers that already looked like an answer.

### Eight for eight — the platform layer is complete

| upstream | lines | this port | lines |
|---|---|---|---|
| videoout + tiling + AGC | ~350 | `pw_videoout_ps4.c` | 170 |
| pad platform block | ~15 | `pw_pad_ps4.c` | 96 |
| audio platform block | ~20 | `pw_audio_ps4.c` | 62 |
| dl platform block | ~70 | `pw_wine_dl_ps4.c` | 73 |
| dmem platform block | ~130 | `pw_wine_dmem_ps4.c` | 121 |
| heap | — | nothing | 0 |
| threads companion | ~7 | `pw_wine_threads_ps4.c` | 8 |
| cwd | — | nothing (28 `--wrap` flags) | 0 |

**530 lines of PS4 platform code.** Underneath it, compiled unmodified: the
13,519-LOC i386 translator, the compositor, the pad core, the audio port
policy, dlopen/dlsym/dladdr with PRXDESC1 parsing, the direct-memory region
allocator, the heap, the thread registry, and the working-directory shim with
its virtual symlink table.

What is left is not a vtable: `tools/build_wine_ps5.sh` and the 81-patch series,
whose constants were *measured* on PS5 firmware rather than ported.

---

## 2026-10-07 — Wine's PE runtime builds (step 1 of 3)

```
wine runtime manifest check passed: 3 modules, 76 data file(s),
  wine=490f6d5dcbb2, digest=429f37b6477f264beec5b719a0d9d857412bf20a39bca20bef515faa115a35e4
```

**Wine 11.17**, the pinned revision, configured `--enable-archs=i386,x86_64
--disable-tests`. Output: `ntdll.dll`, `kernelbase.dll`, `kernel32.dll`, all
`PE32 executable for WINE (DLL), Intel i386`, 11 MB, with 11 MB of NLS data
beside them. The manifest's reproducibility check passed, so a later rebuild of
the same commit gives byte-identical files.

Needed one host package the script does not name: **FreeType development
headers**. Configure's other misses (EGL, X11, GL, v4l2) are warnings; FreeType
is fatal and it is the only one.

### The Wine build is three pieces, not one

Reading `tools/build_wine_ps5.sh` properly changed the estimate:

| piece | needs | state |
|---|---|---|
| host PE runtime | mingw, bison, flex, FreeType | **done** |
| the 81-patch series + ELF link | OpenOrbis SDK | next |
| **PRX link** | `ps5-native-tool`, foundation `30597512…`, branch `exp/prx-module` | **the obstacle** |

⚠ **The PRX link is the part that could stall this.** It converts and signs
Wine's modules into something the console loads, and it runs through PS5 tooling
from a separate repository. Without it the script *skips the PRX link and says
why* — Wine would build as ELF and never become loadable.

The PS4 substitute is OpenOrbis's own `.sprx` path, which demonstrably works:
`libc.prx` and `libSceFios2.prx` in our own packages are exactly that format. So
it is substitutable rather than impossible — but it is a port, not a flag.

⚠ **Vulkan, by contrast, is already in hand.** The script's `--radv DIR` takes a
Mesa RADV checkout instead of Sony's `ps5vk`, and `mesa-build/` holds a working
Gladius-patched one. One of the harder-looking dependencies was solved days
before it was needed.

---

## 2026-10-07 — ⚠ checking what is already adjudicated beats re-measuring it

Zak's correction, and it was right: *"The PS4 and PS5 seem similar, so lots learned
from the PS4 was brought to the PS5. In some cases you are making assumptions on
things that may already be adjudicated."*

The PS4 came **first**. Upstream's PS5 measurements are often rediscoveries of PS4
facts that a ten-year-old homebrew scene already settled, and `orbis-compat`
exists precisely to record them. I had been treating every PS5 constant as needing
fresh measurement here. That is backwards, and it cost a crashed title.

⚠ **A bad probe kills the title, not the console** (Zak, 2026-10-07, correcting
my note above -- the signal probe took the app down, the system stayed up).
GoldHEN survives, FTP survives, and a reinstall-and-rerun is a couple of
minutes. So on-hardware probing is CHEAP and is usually the fastest way to
settle a constant. Do not reason around a question that one throwaway title
would answer. (The thing that genuinely does kill GoldHEN is an FTP login --
see `scripts/ftp-put.py`.)

### Patch 0510 was already settled — no measurement needed

```
orbis_jit.h:54   /// The unit sceKernelMprotect and sceKernelMapDirectMemory work in.
                 /// 16 KiB, not the 4096 a Linux…
orbis_jit.h:58   #define ORBIS_JIT_GRANULE 0x4000u
orbis_mmap.h:77  // rounded up to a 16 KiB page (`addq $0x400f; andq $-0x4000`, so
                 // PAGE_SIZE is 16 KiB on…
```

**16 KiB, same as the PS5**, and derived by disassembling the kernel's own
rounding — better evidence than the probe I was about to run. Patch 0510 applies
unchanged.

### Patch 0501 is genuinely open, and the header proves why

The SDK's `bits/signal.h` declares a **Linux** `ucontext_t`:

```c
typedef struct __ucontext {
    unsigned long uc_flags;        /* FreeBSD puts uc_sigmask FIRST */
    struct __ucontext *uc_link;
    stack_t uc_stack;
    mcontext_t uc_mcontext;
    ...
#define SA_SIGINFO 4               /* FreeBSD: 0x0040 */
#define SA_ONSTACK 0x08000000      /* FreeBSD: 0x0001 */
```

FreeBSD's is `{ uc_sigmask, uc_mcontext, uc_link, uc_stack, uc_flags }`.
`orbis-compat` corrects the `SA_*` flags and `stack_t`, but **not** `ucontext_t`
— so patches 0500 and 0501 are needed here too. What does not transfer is the
PS5's numbers; what does transfer is that FreeBSD 9's layout is **public source**,
so these offsets can be derived and then confirmed rather than discovered by
faulting.

### ⚠ The crash was mine, and reading first would have prevented it

The probe scanned 2048 bytes from `uc`. FreeBSD's `ucontext_t` is around 800;
upstream's +896 fits a larger PS5 frame, not this one. So it ran off the end into
unmapped memory **inside a signal handler**, which is unrecoverable.

`orbis-compat` warns about exactly this area in its own words — *"this port's own
crash reporter has been dying of it since the day it was written"* — and I wrote
a novel faulting probe anyway.

The same file also documents, measured on hardware, that musl's `SA_SIGINFO` of 4
means `SA_RESETHAND` to this kernel, so a handler asking for siginfo is installed
**one-shot and without siginfo**, and arrives with FreeBSD's original
`void(*)(int, int, struct sigcontext *)` signature.

### The rule this earns

Before measuring anything upstream measured on a PS5: check `orbis-compat`'s
corrections, the OpenOrbis headers, and FreeBSD 9's public layout. The PS4 is the
older console and the better documented one. Measure only what none of them
answer — and bound the measurement to a structure size that is known first.

---

## 2026-10-07 — Wine compiles for a PlayStation 4, and the 307 errors are all missing headers

All **83 patches apply cleanly** to pinned Wine 11.17 — zero rejects. Configure
**passes**: Wine accepts the PS4 compiler and takes its FreeBSD paths, which is
the premise the whole port rests on.

### The shim, and three obstacles between CMake and a compiler driver

`tools/build_wine_ps5.sh` requires `$sdk/bin/prospero-clang` by that exact name.
Rather than fork a 600-line script, `build/ps4-wine-sdk/bin/prospero-clang`
presents the OpenOrbis toolchain under it with `ps4-openorbis.cmake`'s own flags.
Getting it to *link* took three goes:

1. **Compile flags are not enough** — configure's first act is "can the compiler
   create executables", which needs the whole link rule: `-nostdlib`, the
   overlay's linker script (not the SDK's `link.x`), and `crt1.o` **last**.
2. **`-fuse-ld=lld` is rejected**: *"unsupported option '-fuse-ld' for target
   'x86_64-scei-ps4'"*. The PS4 driver picks its own linker.
3. **It then wants `orbis-ld`**, Sony's linker name. OpenOrbis ships `ld.lld`, so
   `bin/orbis-ld` is a symlink and the shim prepends its own directory to `PATH`.

CMake sidesteps all three by driving the linker directly instead of through the
compiler driver — which is why the triangle built fine while this did not.

### `-D__PROSPERO__` on purpose, as mechanical triage

Sony sets `__PROSPERO__` for the PS5 and `__ORBIS__` for the PS4: siblings
meaning "Sony console, FreeBSD derived, not Linux". 35 patches gate on it.
Defining it turns all 35 arms on at once so the genuinely PS5-only ones fail and
**name themselves** — faster and more honest than reading 35 patches and guessing.

### The result: zero patch failures

307 errors, and **not one of them is in a patched arm.** Every single one is a
FreeBSD header the OpenOrbis SDK omits:

| failing file | errors | wants |
|---|---|---|
| `ntdll/unix/cdrom.c` | 133 | `sys/cdio.h` — `CDIOC*`, `struct ioc_toc_header`, … |
| `ntdll/unix/file.c` | 63 | `sys/mount.h` — `MNT_RDONLY` |
| `ntdll/unix/system.c` | 28 | `sys/time.h` — `struct clockinfo` |
| `server/sock.c` | 19 | `net/bpf.h` — `BPF_LD`, `struct sock_filter` |
| `ntdll/unix/socket.c` | 7 | `sys/sockio.h` — `SIOCATMARK` |
| `ntdll/unix/signal_x86_64.c` | 7 | `machine/sysarch.h` — `sysarch` |
| `ntdll/unix/server.c`, `env.c` | 14 | `sys/ttycom.h` — `TIOCGWINSZ` |

This is `orbis-compat` §2.2's category exactly — *"Headers the SDK omits although
the platform has them"* — and the overlay already supplies eight of that kind
(`cpuset.h`, `dirent.h`, `endian.h`, `ioccom.h`, `ioctl.h`, `param.h`,
`sysctl.h`, `umtx.h`). Wine needs about six more.

⚠ Note `signal_x86_64.c` fails on **`sysarch`**, not on anything patches 0500 or
0501 touch. I had predicted those two would break first, on the Linux-vs-FreeBSD
`ucontext_t` mismatch. They did not. The prediction was reasonable and wrong, and
the compiler settled it in one run.

⚠ **And a correction to my own first reading.** A grep for errors mentioning
"ps5" returned 299 of 307 — which looked like near-total patch failure. It was
matching the *directory name* `.deps/wine-ps5/` in every path. Matching the error
MESSAGE instead gives **zero**. The same class of mistake as the cwd probe:
instrumentation reporting confidently about itself.

### What this leaves

The patch series is viable on the PS4. What stands between here and `ntdll.so`
is a set of missing FreeBSD headers — bounded, mechanical, and the exact kind of
thing `orbis-compat` exists to carry. After that, the PRX link remains the one
genuinely unknown piece.

---

## 2026-10-07 — 307 → 106 errors on headers alone, and the real blocker surfaces

`src/freebsd-compat/` is 14 headers, ~500 lines: the FreeBSD 9 declarations the
OpenOrbis SDK omits although the platform has them — `orbis-compat` §2.2's
category, which the overlay already carries eight of.

| pass | errors | what changed |
|---|---|---|
| 1 | 307 | none |
| 2 | 251 | cdio, sockio, ttycom, sysarch, bpf, mount, time, filio, conf |
| 3 | **106** | cdio completed; `cdrom.c`'s 132 errors gone entirely |
| 4 | 261 | trap.h and thr.h added — and the count **rose** |

### ⚠ Four traps, each of which made a header look ignored

**An empty header is worse than no header.** `dev/acpica/acpiio.h` was written
deliberately empty so Wine would take its absent-hardware branch. Configure
tests header *existence*, set `HAVE_DEV_ACPICA_ACPIIO_H`, and Wine then included
it and demanded `ACPI_BATT_STAT_CHARGING` from an empty file. Deleted.

**`<sys/ioctl.h>` must chain.** `TIOCGWINSZ`, `SIOCATMARK` and `FIODTYPE` stayed
undeclared long after `ttycom.h`, `filio.h` and `sockio.h` existed, because Wine
includes `<sys/ioctl.h>` and expects them *through* it as FreeBSD's does. The
SDK's does not chain. It looked exactly like the headers were off the path.

**`__BEGIN_DECLS` does not exist in this SDK**, and `fsid_t` is not reachable
from anything `sys/mount.h` includes. Both were compile errors *inside
configure's test*, so `HAVE_SYS_MOUNT_H` came back "no" while the header sat
right there.

**And an unguarded `struct winsize`** in `ttycom.h` made the `sys/mount.h` test
fail with "redefinition of 'winsize'" — a failure reported against the wrong
header entirely.

### The count rising was the useful result

Adding `machine/trap.h` and `sys/thr.h` took configure onto Wine's **FreeBSD**
signal path, and `signal_x86_64.c` went from 7 errors to 131:

```
no member named 'mc_rsp' in 'mcontext_t'
no member named 'mc_rip' in 'mcontext_t'
no member named 'mc_rdi' in 'mcontext_t'   … and the rest of the register file
```

That is the Linux-vs-FreeBSD `ucontext_t` mismatch, and it is **patch 0500's
exact subject**. The SDK declares a Linux `mcontext_t`; Wine's FreeBSD arm wants
FreeBSD's.

⚠ I predicted 0500/0501 would break first, the first build said otherwise, and I
recorded the prediction as wrong. It was **premature, not wrong** — the first
build could not reach that code. Both readings were honest; only the second had
enough headers to be informative.

### Also settled: where a PS4 thread id comes from

`pthread_getthreadid_np` is exported by libkernel, verified against the stub
library. That closes the question flagged during the thread-registry work —
`thr_self` is not exported and musl's `syscall()` uses Linux conventions, but
this is a plain function call needing neither.

### What remains

A FreeBSD `mcontext_t` for the signal path, the BPF guard in `server/sock.c`,
and `getmntinfo`. Then the PRX link, still the one genuinely unknown piece.

---

## 2026-10-07 — ⭐ Wine's Unix side builds for a PlayStation 4

```
dlls/ntdll/ntdll.so         built=True  unresolved=none    3.8M
dlls/win32u/win32u.so       built=True  unresolved=none    8.8M
server/wineserver           built=True  unresolved=none    4.3M
dlls/winevulkan/winevulkan.so  built=True  malloc=imports
dlls/opengl32/opengl32.so      built=True  malloc=imports
dlls/ws2_32/ws2_32.so          built=True  malloc=defines
dlls/crypt32/crypt32.so        built=True  malloc=defines
dlls/dwrite/dwrite.so          built=True  malloc=defines
```

**Eight targets, 28 MB, zero errors.** Wine 11.17, 83 patches, built against the
OpenOrbis toolchain for `x86_64-scei-ps4`.

Error trajectory: **307 → 251 → 106 → 142 → 44 → 23 → 7 → 0.**

### What it took

| | |
|---|---|
| `src/freebsd-compat/` | 15 headers, ~550 lines of FreeBSD 9 declarations the SDK omits |
| `src/wine-patches-ps4/` | upstream's 83, two given PS4 arms |
| `build/ps4-wine-sdk/` | a shim presenting OpenOrbis as `prospero-clang` |

### The shim, and three things CMake hides

The build script requires `$sdk/bin/prospero-clang` by that name. The shim wraps
host clang with `ps4-openorbis.cmake`'s flags, and linking took four discoveries:

1. **Compile flags are not enough** — configure's first act is "can the compiler
   create executables".
2. **`-fuse-ld=lld` is rejected** for `x86_64-scei-ps4`; the driver picks its own.
3. **It wants `orbis-ld`**, Sony's linker name — `bin/orbis-ld` symlinks `ld.lld`.
4. **`-pie` and `crt1.o` are for executables only.** `orbis-ld` refuses
   "-shared and -pie may not be used together", and `crt1.o` carries `_start`
   and the process parameter block, which a `.so` must not define.

CMake sidesteps all four by driving the linker directly rather than through the
compiler driver, which is why the triangle built while this did not.

### ⚠ Five ways I mangled a patch before finding the method

Each failed differently and none said "your patch is incomplete":

1. Hand-written hunk context — corrupt at line 51
2. Edited the text, breaking the hunk's line counts
3. Targeted a line a *later* patch introduces (the `C_ASSERT` comes from 0883)
4. Header extracted with the wrong `sed` range — patch began at `Subject:`
5. **`git diff` omits untracked files**, silently dropping the two new sources
   0883 adds. That one surfaced three layers away: the patch applied, configure
   completed, and `config.status` failed generating Makefiles.

The method that works every time: apply everything before it, apply the patch,
edit the **source**, `git add -A`, then `git diff --cached` beneath the original
header up to the first `diff --git`.

### ⚠ And the ACPI header, wrong in both directions

Written **empty** so Wine would take its absent-hardware branch: configure tests
header *existence*, set `HAVE_DEV_ACPICA_ACPIIO_H`, and Wine demanded
`ACPI_BATT_STAT_CHARGING` from an empty file. **Deleted**: `fatal error: file
not found`, because `system.c` includes it unconditionally under
`#elif defined(__FreeBSD__)`. There is no branch to fall into — on a FreeBSD
target Wine always compiles its ACPI path. It needs the header *and* two
constants, and the PS4's absent `/dev/acpi` makes the ioctl fail at runtime,
which Wine already handles.

### ⚠⚠ COMPILING IS NOT WORKING

The mcontext offset is **a guess**: FreeBSD's +16, where the PS5 measured +64.
Sony has moved it at least once.

- The `C_ASSERT` pins `mc_cs` at 168 in our struct, which independently matches
  the PS5's `+64 + 168 = 232` arithmetic — corroboration of the *layout*, not of
  the offset.
- `mc_len` carries `sizeof(mcontext_t) == 800`, readable at the chosen offset as
  a cheap runtime check.
- Patch 0883's signal-entry **assembly depends on the same number**.

Nothing here has run on the console. A wrong offset means every exception reads
the wrong registers.

## Wine PRX modules build for PS4 (2026-10-07)

All eight Unix-side modules now convert to loadable PRX:
ntdll, win32u, wineserver, winevulkan, opengl32, ws2_32, crypt32, dwrite.

### The export bug that would have wasted a hardware session

`-fvisibility-from-dllstorageclass` is **on by default for the
`x86_64-scei-ps4` target** (with `-fvisibility-nodllstorageclass=hidden`).
It makes `__declspec(dllexport)` the only thing that can export a symbol and
**silently overrides explicit `__attribute__((visibility("default")))`**.

Wine's `DECLSPEC_EXPORT` (include/winnt.h) picks the `visibility("default")`
branch, not the `__declspec` one, because clang satisfies the `__GNUC__ >= 3.3`
test first. So every Unix-side export compiled to `GLOBAL HIDDEN` and the
linker then localized it. The build was clean -- 0 errors, 8 targets, correct
sizes -- and `ntdll.so` exported **only libc**: `__wine_main` was `LOCAL
HIDDEN`, so `pw_prx_lookup(descriptor, "__wine_main")` would have found
nothing at runtime, with no build-time signal anywhere.

Fix: `-fno-visibility-from-dllstorageclass` in CFLAGS. Verified minimal case --
the attribute alone yields `GLOBAL HIDDEN`, with the flag it yields
`GLOBAL DEFAULT`.

⚠ Worth reporting upstream: any Wine-on-PS5 build hits this if Sony's clang
defaults match (it is a PS4/PS5 target feature, not an OpenOrbis one).

### The PRX stage is upstream's, not hand-rolled

`tools/build_wine_ps5.sh` skips its PRX stage on PS4 (`no built
ps5-native-tool` -- it needs ps5-native-app-boilerplate). Converting the `.so`
files directly with `create-fself` is **wrong**: exports come from a generated
**PRXDESC1 descriptor** (`tools/gen_prx_descriptor.py`), not the dynamic
symbol table, so a direct conversion yields a module exporting libc and
nothing prospero-win looks up.

`scripts/prx-ps4.sh` reproduces upstream's four steps, substituting
`create-fself -lib` for the PS5's `ps5-native-tool link --module` + `self
--sign`. Export lists are upstream's verbatim.

Things that had to be right:

- **`.data.sce_module_param` must be its own output section.** lld merges
  `.data.*` into `.data`, after which create-fself crashes rather than
  erroring: `panic: nil pointer dereference` at
  `OELFGenProgramHeaders.go:151`, because it looks the section up by name and
  dereferences nil. `src/freebsd-compat/prx_module_param.ld` keeps it with
  `KEEP` + `INSERT BEFORE .data`.
- **`crtlib.o`, not `crt1.o`** carries that section -- but it also defines
  `module_start`/`module_stop`, which the descriptor defines too. Extract the
  section alone: `llvm-objcopy --only-section=.data.sce_module_param`.
- **DT_NEEDED is what create-fself resolves imports through.** Without linking
  against the already-built modules it reports `missing library for symbol
  (RtlSetLastWin32Error)`. Modules must be built in dependency order, and the
  stub file must be named for the **soname** (`ntdll.prx`), which is what
  create-fself opens verbatim -- a `.so` next to it is not found.
- `libwineshim.a` (this port's stand-in for `wine/ps5/*`) must step aside when
  the real units are linked, or every `__wrap_*` is defined twice:
  `PW_PRX_LINK=1`.
- `link_objects()` drops absolute paths out of make.log, so ntdll's heap and
  direct-memory objects are named explicitly, as upstream's `link_prx` takes
  them as its `$3`.

Recorded dependency graph: `win32u → ntdll`; `opengl32 → ntdll, win32u`;
`ws2_32 → ntdll, win32u, wineserver`.

### Three more header gaps (same musl-vs-BSD shape)

- `RTLD_SELF` -- FreeBSD's `((void *)-3)`, absent from the SDK's dlfcn.h
- `termios` -- musl spells the speed fields `__c_ispeed`/`__c_ospeed`; BSD code
  uses `c_ispeed`/`c_ospeed`. The SDK struct has no member by the BSD name, so
  the macros cannot shadow anything.
- `sys/extattr.h` -- absent; Orbis carries no extattrs and prospero-win stubs
  every entry point, so declarations and namespace constants suffice.

Both resolved with `#include_next`, so the SDK header still leads.

`ptrace` needed a one-word patch to prospero-win instead: the PS4 SDK declares
the address argument `void *` where the PS5 uses `caddr_t`. Same ABI, but C
rejects the conflicting definition.
`src/prospero-win-patches-ps4/0001-compat-ptrace-signature-for-ps4-sdk.patch`.

`pw_wine_thread_set_priority` needed no porting at all: it is plain
`pthread_setschedparam`, and patch 0882 feeds it SCHED_RR with priorities
695-700 -- inside the 256-767 range measured on Orbis.

### Still unverified on hardware

**Nothing of the Wine build has run on the console.** The mcontext offset
remains a guess (+16 from FreeBSD vs the PS5's measured +64), and patch 0883's
assembly depends on the same number; `mc_len == 800` is the intended runtime
check.

## Cross-check against alechurri/nx-on-orbis (2026-10-07)

A port of the Eden Switch emulator to PS4 Pro, on the **same stack we use**:
OpenOrbis 0.5.4, orbis-sdk-v1 (orbis-compat), Mesa RADV, LLVM/libc++ 18. The
closest prior art to this project -- a large C++ codebase with a JIT running
natively on this exact console -- so its platform findings are directly
comparable. Three results:

### 1. The mcontext offset is 64, not 16 -- RESOLVED without a hardware probe

`frontend/ps4_platform.cpp`:

    // PS4 ucontext: 16-byte signal mask + 48 bytes of padding, then a FreeBSD amd64 mcontext.
    constexpr std::size_t Mc = 64;

So PS4 matches the PS5's measured +64 after all; **our +16 guess was wrong**,
and it would have produced garbage registers rather than an obvious failure.
They verified register order against FreeBSD releng/9.0
`sys/amd64/include/ucontext.h`, and every offset they publish -- rdi 8, rbx 64,
rbp 72, r15 120, trapno 128, addr 136, err 152, rip 160, cs 168, rflags 176,
rsp 184, ss 192 -- matches `struct pw_fbsd_mcontext` exactly. Only the base was
ever in doubt.

Fixing the offset also exposed a real error in our struct: it carried an
`mc_spare1[1]` that pushed `mc_fpstate` off its 16-byte alignment and made the
struct 816 bytes. Removed; `sizeof` is now exactly 800, which is what `mc_len`
must read. Patch 0883 takes the offset from the macro, so the correction
propagates with no edit to the patch.

They also note the SDK's `bits/signal.h` describes musl's Linux layout rather
than the kernel's context -- the same orbis-compat trap we hit elsewhere.

### 2. ⚠ OPEN RISK: faults may never reach sigaction on PS4

They install fault handlers with **`sceKernelInstallExceptionHandler`**, not
`sigaction`, and their dynarmic patch excludes the POSIX path outright:

    -elseif (UNIX AND NOT HAIKUOS)
    +elseif (UNIX AND NOT HAIKUOS AND NOT PS4)
    +    # (PS4: faults never reach sigaction there, and the ucontext layout differs; generic below)

**prospero-win uses `sigaction`** (patches 0610, 0710, 0883) and evidently
works on PS5. If this is a genuine PS4/PS5 divergence it hits the core of the
port: the DBT's guard pages, lazy commit, and every guest access violation
depend on SIGSEGV arriving.

Likely fix: install through `sceKernelInstallExceptionHandler` on PS4. The
handler signature is `void (*)(int signal, void *ucontext)` and the ucontext is
the same FreeBSD one, so everything above the offset is reusable. Testable on
hardware cheaply -- take a deliberate fault and see which handler runs.

### 3. They are running a PS4 Pro with the GPU configured as a base PS4

They use the stock orbis-sdk-v1 RADV, described as `"Liverpool" GFX7`. No
`gladius`, no `0x9924`, no `cu_bitmap` or `num_shader_engines` override
anywhere in the tree. `sceKernelIsNeoMode()` appears only in `probe/` to
*print* whether the console is a Pro -- it never reaches the GPU
configuration.

So this project leaves **half the Pro's CUs masked**, which is the bug
`patches/0001-orbis-gladius-neo-detection.patch` fixes: compute knee 72 -> 144
SIMDs, plateau 950 -> 1830 GFLOP/s. They are GPU- and memory-bound (a session
uses ~4.5 of ~4.6 GiB), so it should matter to them. A second independent
consumer of the patch, and a reason to get it upstream.

### Worth borrowing

- **~4.4 GiB usable via package layout** -- more than we have arranged; Wine
  will want it. Their `package-eden.sh` is the reference.
- Their `probe/probe.c` covers firmware, Neo mode, CPU/TSC clocks and boot
  memory pools -- overlapping our probe but wider on memory.
- GFX7 RADV hazard: an indirect draw with a count buffer **hangs the GPU**
  (`patches/eden/0009`). Relevant whenever we reach real rendering.

## Patch 0883's native path is dead code on PS4 (2026-10-07)

`native_libkernel_resolve()` does not merely call `sigaction` -- it resolves
**libkernel's raw sigaction stub** and calls that, because the libc wrapper is
not sufficient on PS5 either. The resolver is bound to one specific PS5
libkernel build:

- a 20-byte fingerprint of the module
- the module name **`libkernel.sprx`**
- an exact four-segment layout: offsets {0, 0x48000, 0x68000, 0x6c000},
  sizes {0x48000, 0x20000, 0x4000, 0x38000}, protections {4, 1, 1, 3}
- imports at `base+0xc3f0` / `base+0xde0`, entry points at `base+0x1600`
  (sigaction) and `base+0x2a0` (sigreturn)

Every one of those fails on PS4. So `pw_native_kernel.sigaction` stays NULL,
`PW_SIGNAL_HANDLER()` selects the non-native handlers, and the whole path falls
back to libc `sigaction` -- the one nx-on-orbis says never receives faults
here. **Upstream's PS5 signal design does not reach PS4 at all**, silently.

Note this also means the PS4 arm added to 0883 earlier (the mcontext offset
override) sits on a path that would never have run.

### The fix, at the seam that already existed

`pw_native_sigaction()` is the single point every handler registration goes
through, so the PS4 arm lives there: the five fault signals (SEGV, BUS, ILL,
FPE, TRAP) install via **`sceKernelInstallExceptionHandler`** -- the PS4 SDK
ships a `libkernel_exception.so` for exactly this, and `libkernel.so` exports
it, though no SDK header declares it. Sony hands the handler
`(int signal, void *ucontext)`; Wine wants `(int, siginfo_t *, void *)`, so a
trampoline synthesises the siginfo. `si_addr` is the only member the fault
paths read, and it is `mc_addr` at mcontext+136. Every other signal still goes
to `sigaction`.

Folded into 0883 rather than added as 0900: the series is capped at 0899 and
0880-0899 are contiguous, and 0883 is what introduces the seam anyway, so the
two PS4 arms belong together.

⚠ **UNVERIFIED. The switch is `PW_PS4_FAULTS_VIA_SONY` (default 1).** Title
**GLDS10004 "Gladius Fault Probe"** (`src/fault-probe`, built and packaged)
settles it in one run: it faults once under each mechanism on its own PROT_NONE
page, recovers by mprotecting the page so the instruction retries, and prints
which handler caught it plus `mc_len` at both +64 and +16. If sigaction turns
out to work, set the switch to 0 and the arm drops out entirely.

The probe reads only named offsets and trusts nothing until `mc_len` reads 800
-- the earlier signal probe scanned a range and took the title down.

## The title itself builds for PS4 (2026-10-07)

`GLDS10005` "Prospero Wine" -- `scripts/build-app-ps4.sh`, packaged at
`pkgout/wine-app/`, 10 MB, Neo attribute set, carrying all eight Wine PRX
modules in `sce_module/`.

Upstream builds the title with `tools/build_native.sh` against
`.deps/ps5-native-app-boilerplate`, which is PS5-only -- but **the title's entry
point is in the repo, not the boilerplate** (`native/wine64_main.c`, 1411
lines). The boilerplate only supplies PS5 packaging, which this port already
replaced. So the app was never actually gated on it.

**12 of the 13 files in `native/` compile for PS4 unmodified**, `wine64_main.c`
included. The one failure was `getdents`, declared locally because the PS5 SDK
omits it where the PS4 SDK declares it with `struct dirent *` and `size_t` --
the same shape as the `ptrace` conflict
(`src/prospero-win-patches-ps4/0002-*.patch`).

### What the binding file does

`src/native/pw_platform_bind_ps4.c` joins the contract to this port. The title
calls `pw_*_ps5_*`; our implementations are named `pw_*_ps4_*` because they were
written as standalone titles first. Upstream's pad/audio/hid cores compile with
their `*_HOST_TEST` guards so the PS5 vtables drop out, and the vtable entry
points land in the binding file instead.

⚠ `*_HOST_TEST` does **not** remove `platform_ops` -- it keeps it as a stub
returning `PW_ERR_UNSUPPORTED`, so the symbol still collides. The build renames
those two stubs aside with `llvm-objcopy --redefine-sym` rather than patching
upstream.

Video out needed a real adapter, not an alias: the contract's `PwVideoOutPs5`
carries `PwAgcPs5` state that the linear-scanout path has no equivalent for, and
its `present()` hand-rolls layout, background fill and a scaled blit through the
tiling tables. On PS4 that is exactly what the portable compositor already
writes, so `present()` is `pw_present_frame_from_view` +
`pw_videoout_ps4_present_scaled`, and the device state stays ours -- the
caller's struct is only a handle, never read by the title.

Not ported, and explicitly stubbed rather than left undefined so the title
starts and says so: **HID** (keyboard/mouse over USB -- the DualShock 4 arrives
through the pad path, which is ported) and **Lapy elevation** (a PS5 privilege
path; a PS4 title under GoldHEN already has what it needs).

### ⚠ What this package is NOT yet

It boots the launcher and exercises module loading. It cannot run a Windows
game: **the package carries no Wine prefix and no PE tree**. The build produces
16 patched PE DLLs (`i386-windows/` and `x86_64-windows/`) but a prefix needs
the whole Wine PE side plus NLS data, and `wowprospero.dll` -- the WoW64 CPU
backend, from `wine/wowprospero` via `tools/build_wowprospero.sh` -- is not
built yet either. Four of upstream's thirteen modules are still missing:
`wowprospero`, `wineps5`, `libfreetype`, `libvulkan`, `xinput1_3`.

So the first run tests: does the eboot start, does `ntdll.prx` load, does the
launcher render, and does the fault path survive -- not whether a game runs.

## Four findings from the nx-on-orbis developer (2026-10-07, by email)

They confirmed our GPU result from their side: *"in my runs the driver came up
with the base PS4 config (mesa.log reports the 18 CU / 72 TMU / 32 ROP spec, and
that value is a spec number, not measured from the hardware), so I never used
the Pro's full 36."* Their port is CPU-bound (0 ms GPU wait, emulated main core
~80% busy in JIT), so it cost them little -- but they want the patches:
*"your patches would matter for anything GPU-heavy, and I'd love to see them."*
The project is closed on their side and explicitly free to fork or reuse.

### 1. wchar_t: does NOT apply to us, and it is worth knowing why

Their report: the SDK's static `wmemchr`/`wmem*`/`wcs*` work on **16-bit**
units while clang's `wchar_t` is 32-bit, so with a newer libc++ `std::find` over
any 4-byte type lowers to `wmemchr` and **silently fails unless the match is the
first element**. They replaced them with 4-byte versions.

Checked here: the `x86_64-scei-ps4` target defines `__WCHAR_TYPE__` as
**`unsigned short`, 2 bytes** -- Sony's ABI default, not a flag we pass (bare
clang for the triple agrees; host Linux says 4). So the SDK's 16-bit routines
are correct for this target and the mismatch does not arise. Confirmed at the
artifact level: `wine_app.elf` and `ntdll.shared.elf` import **zero**
`wmem*`/`wcs*` symbols, Wine defines its own 40, and `liborbis-compat.a`
imports none.

⚠ It is still a tripwire: this holds only while `wchar_t` stays 16-bit here. Any
C++ built for this port with a different triple, or any libc++ code doing
`std::find` over a 4-byte type, brings it straight back -- and the failure is
silent.

### 2. ⚠ RADV flattens each submission into a 2 MiB staging buffer

524288 dwords; a bigger submission **hangs the GPU** (`mesa.log`: "submission
does not fit"). Their mitigation is submitting every 256 draws and after 96
render passes. Applies the moment this port renders anything real, and sits
alongside the other GFX7 hazard already recorded (an indirect draw with a count
buffer hangs the GPU).

### 3. Images cost direct memory on top of the arena

The driver takes one ONION arena (`ORBIS_ARENA_MIB`, default 1024; they ship
1280 because 1024 ran out). Images and other device-local buffers go to
**GARLIC memory allocated outside that arena**, costing direct memory on top of
it -- hundreds of MiB while a race loads. `ORBIS_VRAM_GARLIC=0` keeps them in
the arena instead. So the arena figure is not the memory budget.

Related trap they document: **`sceKernelAvailableDirectMemorySize` returns the
largest free block, not the total.**

### 4. Address space caps at 16 GiB per reservation

The largest single `sceKernelReserveVirtualRange` they got was **16 GiB**;
64 GiB fails with `0x8002000c`.

Checked against what prospero-win asks for, and we are well clear: the reserved
areas are `0x10000-0x68000000` (~1.6 GiB), `0x68000000-0x7f000000` (~368 MiB)
and `0x7f000000-0x7fff0000` (~15 MiB) -- the 32-bit guest's address space, all
far below the cap. It would only bite if this port ever wanted a fastmem-style
aliased view of guest memory, which is exactly what they tried and why they
measured it.

### Also worth having

- Threads get 2 MiB stacks and **64 KiB alternate signal stacks so a stack
  overflow can still report** -- directly relevant to our fault-handler work.
- Their `probe/probe.c` covers firmware, Neo mode, CPU/TSC clocks and boot
  memory pools.

## GoldHEN dies on ANY stray write to its FTP port (2026-10-07)

Known already: lftp's anonymous login kills GoldHEN. Today's addition is that it
is not the *login* that does it -- **one unexpected byte is enough**.

    echo > /dev/tcp/10.168.2.52/2121     # kills it

That opens the control socket and writes a newline. Both GoldHEN services (FTP
2121 and klog 3232) stopped accepting connections immediately afterwards while
the console kept pinging -- the same signature as the lftp failure.

The safe idioms, both already used by `scripts/preflight.sh` and
`scripts/arm.sh`, open the socket and send **nothing**:

    exec 3<>/dev/tcp/HOST/PORT          # bash, connect only
    socket.create_connection(...)        # python, connect only

⚠ So: **probe the console with `scripts/preflight.sh`, never with an ad-hoc
shell one-liner.** The repo's scripts were already correct here; this was a
hand-rolled check run around them. `preflight.sh` also carries a standing
warning against probing the klog port at all, for the same reason.

Recovery is manual: relaunch GoldHEN from the console.

## The fault path and the mcontext, MEASURED (2026-10-07, GLDS10004)

Two assumptions baked into this port were wrong. Both are now settled on
hardware by `src/fault-probe`, which faults once under each mechanism on its
own PROT_NONE page and recovers by mprotecting it so the instruction retries.

### sigaction DOES receive faults on PS4

    sony handler  : receives faults
    sigaction     : receives faults

Both fired. nx-on-orbis's *"faults never reach sigaction there"* does **not**
hold on this console -- their trouble was presumably specific to dynarmic's
handler rather than a platform limit. So patch 0883's PS4 arm is left in place
but **off** (`PW_PS4_FAULTS_VIA_SONY 0`): upstream's sigaction path works and
staying on it keeps the divergence smaller. `ntdll` no longer imports
`sceKernelInstallExceptionHandler` at all.

⚠ This is the second time a finding borrowed from that project did not survive
contact with our console. Their offset was right; this was not. Borrowed facts
are a reason to measure, not a substitute for it.

### The mcontext is at +64 and is 1152 bytes, not 800

`mc_len` read at eight candidate bases, and exactly one is plausible:

    base +0   -> 8593489920
    base +8   -> 16607277028999169
    base +16  -> 6                  (FreeBSD's own offset: garbage)
    base +32  -> 67
    base +48  -> 34074504448
    base +64  -> 1152               <-- plausible
    base +80  -> 131073
    base +96  -> 4218358

So +64 is confirmed, but **Orbis's mcontext_t is 1152 bytes** where FreeBSD 9's
is 800 -- 352 bytes larger, almost certainly a wider xsave area.
`struct pw_fbsd_mcontext` now carries an opaque `mc_tail` to that size; nothing
in the port reads past `mc_ownedfp`, so only the total needed fixing.

**The register block is byte-for-byte FreeBSD 9's**, confirmed field by field:

    mc_trapno  = 12            T_PAGEFLT
    mc_fs/mc_gs= 0x13 / 0x1b   valid selectors
    mc_addr    = 0x200364000   EXACTLY the faulting page
    mc_rip     = 0x405dfd      inside the eboot's text

(The probe's first report of `mc_trapno` as 7599905975566348 was its own bug:
it read 8 bytes at a 4-byte field, packing trapno|fs|gs together. 0x001b0013_0000000c
decodes to exactly the three values above -- which is itself a confirmation.)

### ⚠ Returning from main raises SIGSYS

Both runs ended the same way after `=== done ===`:

    # A user thread receives a fatal signal
    # signal: 12 (SIGSYS)        rip: 0x8000028bc

Restoring the handlers before returning did not prevent it, so it is not ours:
`rip` is outside the eboot, in a library, and SIGSYS is a rejected system call
-- the kernel kills a title that executes a syscall instruction outside
libkernel (the same rule patch 0690 is about). The static libc's exit path
appears to make one.

**Consequence: a title should not return from main.** Harmless in a probe that
has already printed everything, but the real title must not unwind out of
`main` and expect a clean exit.

## The title must not link against ntdll (2026-10-07, measured)

First run of GLDS10005 on hardware died at load:

    # exception: 0xa0020101 (PRX_NOT_RESOLVED_FUNCTION)

and the loaded-module list contained **none of our nine PRX modules** -- only
Sony's `libc.prx` and `libSceFios2.prx`.

Two mistakes, both from linking the title against `prx/stubs/ntdll.so`:

1. **The console does not auto-load `/app0/sce_module/*.prx` to satisfy a
   DT_NEEDED on a non-Sony module.** Sony's own modules load; ours did not, so
   every symbol the title imported from `ntdll.prx` was unresolved before
   `main()`.
2. **The dependency should not exist at all.** prospero-win resolves Wine's
   entry points at RUNTIME through its own loader --
   `src/pw_wine_start.c`: `pw_prx_lookup(descriptor, "__wine_main")` -- so the
   title loads the modules itself and looks symbols up by name.

Worse, the stub link was silently binding the wrong things: the title's 212
undefined symbols were **C++ runtime** (`_ZNSt3__1...`, `_Unwind_*`, `_Exit`),
not Wine at all. Every PRX carries orbis-compat and libc++ whole-archive, so
the linker happily resolved the title's C++ runtime out of `ntdll.prx` instead
of the SDK's. The title now links against Sony libraries only.

One real dependency came out of this: `wine/ps5/pw_wine_prx.c` -- the PRX
loader -- has to be compiled **into the title**. It is what loads the modules,
so it cannot be imported from one.

## prospero-win's launcher runs on the PS4 (2026-10-07)

First real milestone of the port. GLDS10005 renders the launcher -- a Windows
desktop -- on hardware, with upstream's own title code:

    HELLO ps5log/1 title=PPSA99995 app=prospero-win-wine64 tag=ps4-gladius
    PW_WINE64 data_mount   data_after=1 root=/data/prospero-win
    PW_WINE64 library      status=not-found scan_errno=2 entries=0 games=0
    PW_WINE64 args         argc=1 mode=launcher
    PW_WINE64 hid          no PS4 implementation, continuing without keyboard or mouse
    PW_WINE64 launcher     video=ok pad=ok hid=precondition frame=1 apps=0

Every line is correct. `library status=not-found` (errno 2, ENOENT) is simply
that no game library has been pushed yet, and `apps=0` follows from it. The hid
line is this port's own stub reporting itself.

**`video=ok pad=ok` and a drawn frame** means `src/native/pw_videoout_ps4.c` and
`pw_pad_ps4.c` are driving *upstream's* title through the PS5 contract, not our
own test harness: direct memory -> linear scanout -> the portable compositor ->
`sceVideoOutSubmitFlip`, with the binding layer
(`src/native/pw_platform_bind_ps4.c`) joining the two.

The nine PRX modules are in the package but not yet exercised; they load when a
game launches.

### Three bugs stood between a working title and seeing it work

The title had been running the launcher for **five launches** before any of
this was visible. Nothing was wrong with it; the problem was entirely in our
view of it, and each cause was silent by design:

1. **ps5log picked the POSIX network backend.** It is a single-header library
   whose hooks are `#ifndef`-guarded and chosen by what is included *before*
   it. Nothing in the title includes `ps5log_ps5_net.h`, so it compiled against
   `socket`/`connect`/`fcntl` -- which on Orbis are not sceNet. It opened a
   socket that could never connect, took its "connect refused" path, and stored
   the reason in a struct nobody reads.
2. **Nothing calls `sceNetInit`.** Neither ps5log nor `wine64_main.c`; on PS5
   the system evidently does it for a title.
3. **`__attribute__((constructor))` is silently dead in an OpenOrbis eboot.**
   The `.init_array` section exists and holds the entry, but `crt1.o` never
   walks it. What `crt1.o` does carry is a **weak undefined `_init`**, which it
   calls when something defines it -- that is the startup hook on this SDK.

⚠ **Keep a log channel that cannot fail.** `sceKernelDebugOutText` needs no
library, no configuration and no network, so it still reports when the
configured channel is itself the broken thing. The `_init` breadcrumb is
permanent for that reason: four launches were spent blind for want of it.

## Where it stands: dynamic TLS across modules (2026-10-07, end of day)

Wine runs on the PS4. It completes `init_paths`, `virtual_init`,
`init_environment`, `start_main_thread`, `virtual_alloc_first_teb` and
`dbg_init`, and stops inside `server_init_process`:

    PW_WINE64 fault signal=0xb addr=0x0 rip=0x800c99c4d
      module=/app0/win/wine/lib/wine/x86_64-unix/wineserver.so
      module_offset=0xf5c4d

Symbolised, that is `strerror+0x13`, and the instruction is:

    callq  __tls_get_addr
    cmpq   $0x0, (%rax)        <-- rax is NULL

**`__tls_get_addr` returns NULL in a PRX.** `strerror` only happens to be the
victim: it reads musl's thread-local locale pointer. Anything touching dynamic
TLS inside a module will fault the same way.

### Why, and why it keeps coming back

The SDK ships libc **only as a static archive**. Every PRX therefore carries a
private libc with its own thread-local bookkeeping, and the thread running Wine
was created by the *title's* libc. The module's `__tls_get_addr` consults a DTV
its own copy never initialised for that thread.

This is the fourth distinct failure from that one fact, and each looked
unrelated:

1. modules exported libc, so the eboot bound its C runtime to `ntdll.prx`
2. the title's `setenv` and Wine's `getenv` used different environments
3. `wineserver.prx` imported 123 libc symbols from `ntdll.prx`
4. dynamic TLS has no valid DTV in a module

### The options, none free

- **Link the shared libc** (`libSceLibcInternal`, the stub for the `libc.prx`
  the process already loads). This is almost certainly how PS5 avoids all four:
  one libc, one TLS regime. Tried once and reverted -- it pulled 88 symbols
  including `malloc`, `exit` and the `__cxa_*` family, and the title died with
  `PRX_NOT_RESOLVED_FUNCTION`. Worth retrying now that the link order is fixed,
  since that earlier attempt also had problem (3) in play.
- **Force initial-exec TLS** (`-ftls-model=initial-exec`) so accesses skip
  `__tls_get_addr`. Does not help here: `strerror` comes from the prebuilt
  `libc.a`, which we cannot recompile.
- **Supply our own `__tls_get_addr`** in each module, backed by that module's
  own TLS segment. Self-contained, but it is a runtime-internals
  reimplementation and would need care around thread creation and teardown.

The first is the principled fix and the one to try next.

### Worth keeping regardless

`module=`/`module_offset=` in the emergency line is what made this findable at
all; a bare `rip` named nothing once faults moved outside ntdll. Any address in
a loaded module can now be symbolised against the unstripped `.shared.elf` kept
in `.deps/wine-ps5/prx/`.

## The shared libc fixes dynamic TLS (2026-10-07)

`PW_SHARED_LIBC=1` resolves libc from `libSceLibcInternal` instead of the static
archive, so the title and every PRX share one libc -- one `environ`, one TLS
regime. `ntdll.prx` no longer defines `__tls_get_addr` at all.

**It works.** Wine now gets *through* `server_init_process` without faulting,
and the whole class of four static-libc failures goes with it. The earlier
attempt failed for reasons since fixed: the link order that had modules binding
their C runtime to ntdll, `LDLIBS` being single-quoted so the switch never
expanded, and the missing late archive pass `__inet_aton` needs.

### What stops it now: Wine exits silently

    WINE wine startup: server_init_process
    PW_WINE64 exit sink_calls=7
    PW_WINE64 restart to=launcher cycle=1 reason=wine-exit

`reason=wine-exit` is `__wine_main` returning, not a fault. `sink_calls=7`
accounts for exactly the seven startup lines above it, so Wine shut down
deliberately and said nothing.

### ⚠ Wine's debug channels are not usable yet

Setting `WINEDEBUG=+server,+seh,+process,+module` changes the outcome from a
clean exit to:

    signal: 12 (SIGSYS)   rip: 0x8000028bc

the same address and signal the fault probe hit at exit this afternoon. SIGSYS
here is the kernel killing a title for executing a `syscall` instruction
outside libkernel -- patch 0690's subject. So the tracing path reaches code
that issues a raw syscall, and turning the channels on destabilises rather than
illuminates. Reverted.

That it changed the outcome at all confirms the environment now reaches Wine,
which is itself the shared-libc fix working.

### Next

1. Find why `__wine_main` returns. The silent exit is the blocker; Wine's own
   tracing is currently unavailable, so the next lever is either the raw-syscall
   problem (so channels become usable) or instrumenting the return path
   directly.
2. The raw-syscall/SIGSYS issue is worth solving on its own: it is the same
   fault that kills a title returning from `main`, and it will keep appearing.

## The SIGSYS was fatal_error; the real failure is create_server_dir (2026-10-07)

SIGSYS is **not catchable** on this kernel -- it kills the title before any
handler runs, so installing one changed nothing. What does work: the crash dump
carries a **dynamic libraries** section listing every module's text range, and
the title now logs its own segment bases at load, so klog's backtrace can be
symbolised afterwards against the unstripped `.shared.elf` files.

Doing that turned a bare list of addresses into:

    wineserver+0x8a0e6   fatal_error
    wineserver+0x8ba16   create_server_dir
    wineserver+0x8bc52   open_master_socket
    wineserver+0x69a63   start_inprocess_server
    ntdll+0x7e507        connect_inprocess_server
    ntdll+0x7cc25        server_connect
    ntdll+0x6d544        start_main_thread

So the SIGSYS is a **red herring**: `create_server_dir` fails, calls
`fatal_error`, and `fatal_error` dies trying to write its message to a stderr
that goes nowhere on this console. The message that would have told us
everything is the thing that killed the process.

### ⚠ A prefix written over FTP is not writable by the title

Same ownership problem seen earlier in reverse: `/data/prospero-win` was created
by the title and the FTP server could not write into it until `SITE CHMOD 777`.
The prefix pushed by `pw_prefix.py` belongs to the FTP server, and the title
has to create its server directory inside it. Chmod the library root, the
prefix and its drive_c after any push.

### Also confirmed by this dump

`libSceLibcInternal.sprx` **is loaded** (text 0x800090000-0x8001b8000), which
settles the doubt about whether `PW_SHARED_LIBC` could bind at runtime. It can,
and does.

### Worth keeping

Logging module segment bases at load costs three lines and is the only reason
an uncatchable crash is diagnosable at all. Combined with the dump's own module
list, any address in any module can be named after the fact.
