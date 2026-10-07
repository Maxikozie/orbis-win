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
| 5 | prospero-win port | — |

Target console: PS4 Pro, firmware 9.60, GoldHEN.

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
