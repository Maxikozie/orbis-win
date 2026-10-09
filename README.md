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

*Last session: 2026-10-08. Read [`docs/FINDINGS.md`](docs/FINDINGS.md) from
"The bug that was under everything" for the full record.*

GTA IV is the title being used to exercise the runtime, and for most of a day
its black screen was read as a graphics problem. It was not.

**No thread this port created had ever run.** `create_server_thread` closed the
request pipe after handing it to the in-process wineserver — correct where
`SCM_RIGHTS` duplicates a descriptor into the receiver, fatal here, where `dup`
is `EPERM` for every kind of descriptor (`src/dup-probe`) so the fd is passed
*by number* into a shared table. The server read `EBADF` on each new thread's
first request and killed it:

```
wineserver: protocol error 0028: read: Bad file descriptor
wineserver: kill_thread 0028 exit=1 violent=1 from fatal_protocol_error+0xcf
```

One guard (wine patch `0866`) and the whole stack above it came alive: eighteen
threads complete `init_thread`, reach `BTCpuThreadInit`, run guest code, winmm
starts its devices thread, `waveOutOpen` returns, and the alive tick reads
`audio=1027` where it had read `audio=0` since the port began.

Everything graphics is finished and was never the blocker — DirectComposition
device, target and visual, a 1920x1080 swapchain on the scanout's own extent,
both back buffers handed to the game (wine patches `0860`, `0861`, `0862`,
`0879`).

### The open question

`pthread_create` now refuses at about a dozen threads:

```
wine-ps5: spawn_thread tid=0068 FAILED pthread_create=-2147352541 threads=13 ...
```

`-2147352541` is `0x80020023` — an SCE call behind a POSIX name answers
`0x8002_0000 | errno`, so errno `0x23` = **EAGAIN**, a resource ceiling rather
than a heap. (Wine maps every `pthread_create` failure onto `STATUS_NO_MEMORY`,
which sent this the wrong way for a while; patch `0867` keeps the real number.)

`src/thread-probe` answers which resource. Five trials, each creating until
refused, threads parked so it measures how many can exist at once:

| | |
|---|---|
| A | Wine's own: 1 MiB caller-supplied stack, system scope — the number to beat |
| B | 256 KiB stack — is it the stack's size |
| C | the library's own stack — is it `setstack` at all |
| D | process scope — is it the kernel thread |
| E | A again, after A–D are joined — concurrent threads, or total creations |

### Resuming

```sh
./scripts/logs.sh klog &               # FIRST. See the warning below.
./scripts/build.sh threadprobe --pkg --neo --deploy
./scripts/build-app-ps4.sh --pkg --neo && ./scripts/deploy.sh wine-app
```

Install from `/data/pkg/` and run the thread probe before the runtime — it is
6 MB, answers the open question by itself, and depends on nothing else. Then
GTA IV, to see how far it gets now that threads run and audio flows. Session
logs come off with `scripts/ftp-get.py` from `/data/prospero-win/logs/`;
`next.txt` names the next slot, so the run that just finished is `(next+7)%8`.

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
