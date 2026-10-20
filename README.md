# orbis-win

**Current PS4 Wine port handoff:** [docs/HANDOFF-2026-10-20.md](docs/HANDOFF-2026-10-20.md).
The repository carries the PS4 runtime port, build and packaging tools, compatibility patches, and hardware test notes.

Bringing RADV — and eventually a Windows runtime — up on a **PlayStation 4 Pro**,
running on Orbis OS itself. No Linux, no dual boot.

The hard dependency already exists: [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4)
is Mesa's RADV running natively on the PS4 GPU, submitting through `sceGnmSubmitCommandBuffers`,
presenting through VideoOut. It passes Vulkan CTS on hardware and runs OpenGothic at ~50 fps.

**The Pro path is active, but not complete.** On the PS4 Pro, RADV identifies
Neo mode as `CHIP_GLADIUS`; a hardware-ID probe measured four shader engines
and 36 active CUs. Raster-config and CU-mask experiments have run, but the
Neo GTA IV workload still hangs and no Vulkan CTS run has completed on Gladius.
The Orbis winsys currently reports GFX IP version 7.2 in both title modes, so
the printed `gfx=9` means Mesa enum `GFX7` (ordinal 9), not GFX9. Whether that
GFX-level choice correctly models the Pro's custom Polaris-era hardware remains
an open driver question; see [`docs/HANDOFF-2026-10-09.md`](docs/HANDOFF-2026-10-09.md).

So this is not a port. It is a bring-up, and most of it is measurement.
[`docs/PLAN.md`](docs/PLAN.md) has the five stages;
[`docs/FINDINGS.md`](docs/FINDINGS.md) is the running record.

## Status

| Stage | | |
|---|---|---|
| 1 | Toolchain → PKG, on the host | **done** |
| 1 | Triangle on the television | **done** — 1024+ frames, 1920x1080 |
| 2 | GPU identity + topology probe | **done** — knee measured, 72 → 144 SIMDs |
| 2b | Neo mode (`--neo`) in the package | **done** |
| 2c | Gladius detection in RADV | **done — 36 CU confirmed live** |
| 3 | CTS, and the ring-sizing deltas | — |
| 4 | OpenGothic, for a frame rate | — |
| 5 | prospero-win port | **started** — video, pad and audio working; threads fixed 2026-10-08, see [Where this stands](#where-this-stands) |

Target console: PS4 Pro, firmware 9.60, GoldHEN.

## Where this stands

*Last session: 2026-10-09. Read [`docs/FINDINGS.md`](docs/FINDINGS.md) from
"The bug that was under everything" for the full record.*

GTA IV is the title being used to exercise the runtime, and for most of a day
its black screen was read as a graphics problem. It was not. Everything
graphics is finished and was never the blocker — DirectComposition device,
target and visual, a 1920x1080 swapchain on the scanout's own extent, both back
buffers handed to the game (wine patches `0860`, `0861`, `0862`, `0879`).

**Every problem since has been the same shape**, and it is worth stating up
front because it predicts the next one: *prospero-win is correct code carrying
PS5 assumptions, and the PS4 differs underneath it in ways that only surface
three layers away.*

| assumption | PS5 | PS4 |
|---|---|---|
| a descriptor can be duplicated | `dup` works | **EPERM** → passed by number → the `close()` that killed every thread |
| a thread's DBT arena | direct memory, "up to 12 GiB" | **448 MiB flexible, total** |
| a default thread stack | modest | **16 MiB** |
| libc heap | ~13 MiB | **>1 GiB** — the port's own heap is the tighter one here |
| `__tls_get_addr` | works | returns NULL → `-femulated-tls` |
| `fprintf(stderr)` | arrives | **goes nowhere**, the port's own watchdogs included |

None of those is an API difference a header would reveal. The fd bug surfaced
as *a DirectSound notification never firing*.

### Fixed: no thread this port created had ever run

`create_server_thread` closed the request pipe after handing it to the
in-process wineserver — correct where `SCM_RIGHTS` duplicates a descriptor into
the receiver, fatal here, where the fd is passed *by number* into a shared
table. The server read `EBADF` on each new thread's first request and killed
it:

```
wineserver: protocol error 0028: read: Bad file descriptor
wineserver: kill_thread 0028 exit=1 violent=1 from fatal_protocol_error+0xcf
```

One guard (wine patch `0866`) and the stack above it came alive: threads
complete `init_thread`, reach `BTCpuThreadInit`, run guest code, winmm starts
its devices thread, `waveOutOpen` returns, and the alive tick reads
`audio=1027` where it had read `audio=0` since the port began. The process also
survives now — `0882` guards an exit path that assumed every thread is a Wine
thread, and a run lasted 245 s where it used to fault on teardown.

### Open: the thread ceiling is memory, not threads

`pthread_create` refuses at about a dozen with `0x80020023` — an SCE call
behind a POSIX name answers `0x8002_0000 | errno`, so errno `0x23` = **EAGAIN**.
Wine maps every `pthread_create` failure onto `STATUS_NO_MEMORY`, which sent
this the wrong way for a while; `0867` keeps the real number, and `0868` asks at
the point of refusal what flexible memory says and whether a smaller stack is
taken.

`src/thread-probe` (GLDS10009) settled what the console can do, and killed two
of my theories in one run:

```
A  wine's own: 1 MiB stack, system scope   created 146, flexible 1024K
B  256 KiB stack, system scope             created 192 (the cap), 7168K
C  library's own stack (~16 MiB each)      created  28, flexible 0K
F  1024 mappings at once                   the cap, never refused
G  unmapped 1024, flexible fully returned, remapped 1024
H  after 2048 mprotect splits              created 141
```

A bare title makes **146** threads with exactly the attributes Wine asks for,
so nothing is wrong with the mechanism. `EAGAIN` arrives when flexible memory
is nearly gone — and in the runtime it is: `flexible=3072K` of 449536K. VM
entries are *not* the resource (H barely moved; G showed every mapping comes
back), which was my guess and was wrong.

Where it went — `0014` lists individual regions, because the bucket could not:

```
flexible_holder rank=1 bytes=378880K count=24 prot=0x3 name=orbis
flexible_region rank=2 at=7d7bb0000 bytes=16384K prot=0x3 name=orbis
flexible_region rank=3 at=7d8bb0000 bytes=16384K prot=0x3 name=orbis
```

370 MiB in contiguous 16 MiB regions, one appearing per thread. `orbis` is just
the kernel's name for any anonymous mapping, so the bucket name distinguishes
nothing; the addresses do.

**The DBT hypothesis was wrong.** Two runs of `0015` left the regions at
16 MiB. `0016` records the actual allocations: secondary DBT arenas are
8 MiB at `0x100...` addresses, not the `0x7d...` regions, and their setup
usually leaves flexible memory unchanged. They already use Wine's direct-memory
path. Do not move executable memory to solve this symptom.

**The linked SDK thread interposer explains the 16 MiB regions.** Wine asks
for a supplied 1 MiB kernel stack. The compatibility layer uses the calling
thread's stack size as a floor (16 MiB here), raises Wine's request, and
constructs new attributes without copying its supplied address. The kernel
therefore allocates a fresh 16 MiB flexible stack for every worker.
The bare thread probe does not exercise the same linked interposer.

`src/native/pw_pthread_stack_ps4.c`, linked into ntdll with
`--wrap=pthread_create`, sends explicitly supplied stacks directly to
`scePthreadCreate`. Calls without a supplied stack retain the SDK path.
Wine installs its own alternate signal stack in `init_thread_pipe`.
The wrapper reports both requested and actual stack addresses and sizes.
**Confirmed on hardware:** the 09:00 run uses the supplied 1 MiB stacks
and retains 290816K of flexible memory. GTA IV then fails earlier in its
1911.dll D3D12 path with a Vulkan map error and guest access violation.
The application closes without a host crash. Pinball is the next baseline.

Do not credit the RADV `--wrap=scePthreadCreate` (4 MiB default stacks) with
the earlier 13 → 19 thread improvement: identical mapped memory across those
runs never established its effect. The ntdll stack override is a separate path.

### Resuming

The latest diagnostic package includes the supplied-stack fix, `0016`
allocation measurements, and Pinball input/font patches `0869` and `0821`. Install `/data/pkg/wine-app.pkg` after upload;
launching the old installed package cannot test a newly uploaded build.
The corrected GTA IV profile lives in `src/profiles/gtaiv.profile` and was
uploaded separately: 1920x1080, upstream launch arguments, and XInput,
with the intentional `d3d9=n` override retained for this copy's 1911.dll path.
Pinball is the confirmed baseline: the clean `ORBW00024` package works, and
the user confirms its presentation looks correct. Controls work with patch
0869, and System/MS Sans Serif load with patch 0821. Windows decorations are
intentionally omitted for the console presentation. See the current handoff
for the composition fix and the next title tests.

```sh
./scripts/logs.sh klog &               # FIRST. See the warning below.
python3 ./scripts/ps5logd.py &         # runtime TCP logs, port 9300; only one receiver
./scripts/build-app-ps4.sh --pkg --neo && ./scripts/deploy.sh wine-app
./scripts/build.sh threadprobe --pkg --neo --deploy    # only if a number is disputed
```

For a side-by-side GTA IV Neo/base comparison, install the Neo package as
`GLDS10005` and the base package as `GLDS10041`. Deploy them separately with
`./scripts/deploy.sh wine-app` and `./scripts/deploy.sh wine-app-base`. To
rebuild the base package without overwriting the default Neo output:

```sh
TITLE_ID=GLDS10041 TITLE="Prospero Wine Base" \
  PKG_OUT="$PWD/pkgout/wine-app-variants/GLDS10041-base" \
  ./scripts/build-app-ps4.sh --pkg
```

Install from `/data/pkg/` and launch Pinball. Press Options (F2), Cross
(Space), and L1/R1 (Z/Slash). Check runtime logs for `input queue … status=0`,
`key hwnd=… vk=… status=0`, and font families System/MS Sans Serif/Tahoma.
The previous run accepted native input packets but dispatched no Windows
keys; bitmap fonts failed through FreeType's libc file-opening path.
Patch 0869 retains the input descriptor rather than duplicating it and drains
it using poll because nonblocking mode is refused. Patch 0821 reads font
metadata from already mapped bytes, with path-resolution diagnostics for the
still-missing Tahoma. The user also reported the window border disappearing;
frame dimensions remained 606x467, so do not assume a fullscreen mode switch.
Console file logs are available through `scripts/ftp-get.py` from
`/data/prospero-win/logs/`; `next.txt` names the next slot, so the run that just
finished is `(next+7)%8`.

⚠ **Rebuilding wowprospero or ntdll needs the whole chain**, in this order, or
the package ships a module that does not match the patches:

```sh
./prospero-win/tools/build_wine_ps5.sh \
    --patches "$PWD/src/wine-patches-ps4" --sdk "$PWD/build/ps4-wine-sdk"
./scripts/prx-ps4.sh
for s in radv freetype wineps5; do ./scripts/$s-prx-ps4.sh; done   # see below
./scripts/build-app-ps4.sh --pkg --neo && ./scripts/deploy.sh wine-app
```

`build_wine_ps5.sh` always reports "build failed" — it links `ntdll.so` twice
and the first attempt collides with `libwineshim.a`. Check
`dlls/ntdll/ntdll.so: built=True` in its output instead of its exit status.

⚠ **Start the klog receiver before anything else.** `wineserver`'s diagnostics
— `pw_init_thread_note`, `kill_thread`, `fatal_protocol_error` — go to klog and
nowhere else. It being dead cost most of 2026-10-08: the server had been naming
the exact failure all day into a channel nobody was recording.

⚠ **`fprintf(stderr)` reaches nothing on this console**, including this port's
own diagnostics. Use `ERR()` in ntdll (it arrives even from a thread with no
TEB) and `sceKernelDebugOutText` in wineserver.

⚠ **Build the normal PS4 Pro package with `--neo`.** `build-app-ps4.sh` defaults
to `NEO=0`, and a package built without it runs a Pro as a base PS4 — the only
sign is `device "PS5" 1002:9920` in the log. The separately named
`GLDS10041` package above is the deliberate base-mode comparison build.

⚠ **`scripts/prx-ps4.sh` empties `prx/sce_module`** and rebuilds only its own
nine modules. Run `radv-`, `freetype-` and `wineps5-prx-ps4.sh` after it, or the
package ships 50 MB light with no Vulkan driver. A good package is ~421 MB with
twelve modules.

⚠ **A patch regenerated with `git diff` against the fully applied tree** carries
context from patches later in the series and will not apply at its own position.
Put the change after the patch that owns that code, or fold it in — the NULL
`thread_data` guard went into `0882`, which introduces the thread that has no
Wine data. The base series is `0100-0899`; local follow-up patches `0900-0961`
are appended after it, and the build accepts through `0961`.

⚠ **`prospero-win/` is gitignored and is its own tree.** Work there survives
only as a patch in `src/prospero-win-patches-ps4/`. Verify one with
`git -C prospero-win apply --check --reverse <patch>` — if it reverses cleanly
it captures exactly those changes and nothing another patch already owns.


## Quickstart

```sh
./scripts/prereqs.sh                   # what the host needs; prints the apt line
./scripts/fetch-sdk.sh                 # orbis-sdk-v1, checksummed and verified
./scripts/build.sh hello --pkg         # layout only — touches no Mesa
./scripts/build.sh triangle --pkg      # the graphics stack, ~26 MB .pkg
./scripts/build.sh probe --pkg         # stage 2: what the driver says it is on
```

Then point it at the console:

```sh
cp config.env.example config.env       # put your PS4's IP in it
./scripts/preflight.sh                 # console up? which port is FTP on?
./scripts/logs.sh                      # leave running in its own terminal
./scripts/build.sh triangle --deploy   # uploads to /data/pkg/
```

`preflight.sh` probes for the FTP banner across 2121, 1337 and 21 rather than
trusting the configured port — guessing wrong there looks like a hung upload
rather than a refused one, which is an hour you do not get back.

Install from `/data/pkg/` on the console and launch it. The two examples answer
different questions on purpose — `hello` proves the bundle is laid out right with
nothing else able to fail, `triangle` proves the graphics stack reaches the
screen. One example doing both would report a driver fault as a layout fault.

## Titles

What each build target is called on the console. `build.sh` and `deploy.sh` take
the left-hand name; the home screen shows the right-hand one.

| target | title ID | on the device |
|---|---|---|
| `hello` | GLDS10000 | Gladius Hello |
| `triangle` | GLDS10001 | Gladius Triangle |
| `probe` | GLDS10002 | Gladius Probe |
| `present` | GLDS10003 | Prospero Presenter |
| `fault` | GLDS10004 | Gladius Fault Probe |
| `wine-app` | GLDS10005 | the prospero-win runtime |
| `modprobe` | GLDS10006 | Gladius Module Probe |
| `statprobe` | GLDS10007 | Gladius Stat Probe |
| `dupprobe` | GLDS10008 | Gladius Dup Probe |
| `threadprobe` | GLDS10009 | Gladius Thread Probe |
| `mapprobe` | GLDS10010 | Gladius Mapping Probe |
| `vkmapprobe` | GLDS10011 | Gladius Vulkan Mapping Probe |

The presenter is the first prospero-win code to put a frame on a PS4.

Read them back out of a built package with the SFO parser in `docs/FINDINGS.md`
rather than trusting this table — an installed title keeps the name it was
installed with, so a stale install can disagree.

## Scripts

| | |
|---|---|
| `prereqs.sh` | Checks the host toolchain. Never installs anything. |
| `fetch-sdk.sh` | Downloads, checksums, unpacks and runs the bundle's own `verify.sh`. |
| `build.sh` | `{hello\|triangle} [--pkg] [--deploy]`, plus the ELF checks that catch a bad link. |
| `preflight.sh` | Console reachable, which port serves FTP, log port free, pkg built. |
| `wait-for-console.sh` | Polls until the PS4 answers, then runs preflight. |
| `logs.sh` | UDP netlog capture (primary) or GoldHEN klog (fallback). |
| `capture-radeon-info.sh` | Stage 2 — pulls the dump out of a log and diffs it against Liverpool. |

`sdk/` holds the 92 MB bundle and is gitignored; `fetch-sdk.sh` recreates it.

## Sources

- [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4) — RADV on PS4, branch `orbis`
- [orbis-ports/ps4-mesa-docs](https://github.com/orbis-ports/ps4-mesa-docs) — the port's record
- [orbis-sdk-v1](https://github.com/orbis-ports/orbis-porting-kit/releases/tag/orbis-sdk-v1) — the pinned SDK bundle
- [mpereiraesaa/prospero-win](https://github.com/mpereiraesaa/prospero-win) — the PS5 Wine runtime this is eventually aimed at


### Current mapping experiment (2026-10-09)

The direct-memory kernel probe passed: Onion and Garlic CPU aliases below
4 GiB had zero mismatches, including offset aliases and cleanup. Mesa's placed
CPU mapping implementation is built and saved in `src/mesa-patches-ps4/`.
The corrected Vulkan probe passed all three compatible memory types, including
cached GTT: six GPU copies, zero mismatches, reserve/remap and cleanup passed
(`DONE tested=3 result=PASS`). Next: close the probe, install the already
uploaded `/data/pkg/wine-app.pkg`, and launch GTA IV to test Wine's integration.
Klog receiver
is PID902402 writing `build-ps4-logs/ps4-klog-20261009-094758.log`.
The rebuilt Wine package contains the new Vulkan driver; console installation
is pending. `radv-prx-ps4.sh` now defaults to the local Mesa build.


Latest GTA IV run confirmed low placed maps (0xd40000) and GPU submissions,
then a later map failed (-5) and the guest faulted. Fixed the alias's rejection
of4 KiB BO sizes by rounding to the same16 KiB kernel page as the GPU mapping.
The expanded `/data/pkg/vkmapprobe.pkg` tests4 KiB as well as64 KiB buffers;
hardware rerun passed both sizes on all three compatible memory types:
12 GPU copies with zero mismatches. Wine package is rebuilding with this refinement.


GTA IV with page rounding now runs the Razor1911 intro, then initializes DXVK
and faults during shader compilation: NIR allocations return NULL from the
console libc heap. RADV PRX now uses ntdll's Wine heap for ordinary/aligned and
allocating string functions; build and host checks pass. Replacement Wine
package is uploaded at `/data/pkg/wine-app.pkg`; reinstall and launch GTA IV.
Shader compilation hardware verification remains pending.


2026-10-09: user confirmed GTA IV reached the loaded scene with the heap
corrected Vulkan PRX. Runtime passed the previous shader-allocation fault,
created low placed mappings and reached a1920x1080 swapchain with frame
submissions. Sound is unconfirmed; continue checking controls and sustained
play rather than declaring compatibility complete.
