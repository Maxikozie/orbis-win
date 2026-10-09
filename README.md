# ps4-gladius

Bringing RADV — and eventually a Windows runtime — up on a **PlayStation 4 Pro**,
running on Orbis OS itself. No Linux, no dual boot.

The hard dependency already exists: [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4)
is Mesa's RADV running natively on the PS4 GPU, submitting through `sceGnmSubmitCommandBuffers`,
presenting through VideoOut. It passes Vulkan CTS on hardware and runs OpenGothic at ~50 fps.

**It has never run on a Pro.** `CHIP_GLADIUS` is declared in the driver, PCI
`0x9924` maps to it, and every Liverpool workaround is written
`family == CHIP_LIVERPOOL || family == CHIP_GLADIUS` — but every captured dump,
every CTS run and every frame in that project's record came off a base PS4
(Liverpool, 18 CU). The Pro path is written and unexercised.

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

**The current hypothesis** is that these are the DBT's per-thread code arenas.
`thread_budget.h` sizes them against *"direct memory … out of up to 12 GiB"* —
the PS5's budget — at 32 MiB per secondary thread, and `pw_wow_thread_fit`
halves on refusal, which is exactly why they measure 16 MiB and not 32.
Patch `0015` sets 8 MiB for secondary threads on PS4 (the first thread keeps its
128 MiB; that is the arena a game's hot loop must fit in).

⚠ **`0015` is a measurement as much as a fix, and the next run decides it.**
That those regions are arenas is *inferred* — from the address range being
`place_above_guest`'s, and the count tracking the thread count — not proven;
`prot=0x3` means they would be the read-write half of the engine's write/exec
pair. If `flexible_region` drops to `8192K`, they are arenas. If it stays at
`16384K`, the attribution is wrong and the next step is not the one it looks
like.

⚠ Do not credit the RADV `--wrap=scePthreadCreate` (4 MiB default stacks) with
the 13 → 19 thread improvement. Halved arenas explain that equally well, and
`mapped=461024K` was identical to the byte before and after it, which it should
not have been if 288 MiB of Mesa stacks had been freed. The wrap is right on its
own terms and is kept.

**After that**, the proper fix is to put the arenas back in direct memory, where
the PS5 keeps them and where this console has gigabytes rather than megabytes.
That lands in executable memory and the write/exec alias, which is not where to
go while the consumer is still an inference.

### Resuming

```sh
./scripts/logs.sh klog &               # FIRST. See the warning below.
./scripts/build-app-ps4.sh --pkg --neo && ./scripts/deploy.sh wine-app
./scripts/build.sh threadprobe --pkg --neo --deploy    # only if a number is disputed
```

Install from `/data/pkg/` and run GTA IV. Session logs come off with
`scripts/ftp-get.py` from `/data/prospero-win/logs/`; `next.txt` names the next
slot, so the run that just finished is `(next+7)%8`. Read, in this order:
`flexible_region … bytes=` (8192K or 16384K — that is the open question),
`available=`, whether `spawn_thread … FAILED` appears at all, and then how far
the game gets.

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

⚠ **Always build with `--neo`.** `build-app-ps4.sh` defaults to `NEO=0`, and a
package built without it runs a Pro as a base PS4 — the only sign is
`device "PS5" 1002:9920` in the log.

⚠ **`scripts/prx-ps4.sh` empties `prx/sce_module`** and rebuilds only its own
nine modules. Run `radv-`, `freetype-` and `wineps5-prx-ps4.sh` after it, or the
package ships 50 MB light with no Vulkan driver. A good package is ~421 MB with
twelve modules.

⚠ **A patch regenerated with `git diff` against the fully applied tree** carries
context from patches later in the series and will not apply at its own position.
Put the change after the patch that owns that code, or fold it in — the NULL
`thread_data` guard went into `0882`, which introduces the thread that has no
Wine data. The series is `0100-0899` and `0870-0899` is contiguous, so new wine
patches go in the free `0821-0869` range.

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
