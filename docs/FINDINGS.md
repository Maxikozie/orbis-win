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
