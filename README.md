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
| 1 | Triangle on the television | not yet run |
| 2 | `radeon_info` captured on Gladius | — |
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
```

Then point it at the console:

```sh
cp config.env.example config.env       # put your PS4's IP in it
./scripts/logs.sh                      # leave running in its own terminal
./scripts/build.sh triangle --deploy   # uploads to /data/pkg/
```

Install from `/data/pkg/` on the console and launch it. The two examples answer
different questions on purpose — `hello` proves the bundle is laid out right with
nothing else able to fail, `triangle` proves the graphics stack reaches the
screen. One example doing both would report a driver fault as a layout fault.

## Scripts

| | |
|---|---|
| `prereqs.sh` | Checks the host toolchain. Never installs anything. |
| `fetch-sdk.sh` | Downloads, checksums, unpacks and runs the bundle's own `verify.sh`. |
| `build.sh` | `{hello\|triangle} [--pkg] [--deploy]`, plus the ELF checks that catch a bad link. |
| `logs.sh` | UDP netlog capture (primary) or GoldHEN klog (fallback). |
| `capture-radeon-info.sh` | Stage 2 — pulls the dump out of a log and diffs it against Liverpool. |

`sdk/` holds the 92 MB bundle and is gitignored; `fetch-sdk.sh` recreates it.

## Sources

- [orbis-ports/mesa-ps4](https://github.com/orbis-ports/mesa-ps4) — RADV on PS4, branch `orbis`
- [orbis-ports/ps4-mesa-docs](https://github.com/orbis-ports/ps4-mesa-docs) — the port's record
- [orbis-sdk-v1](https://github.com/orbis-ports/orbis-porting-kit/releases/tag/orbis-sdk-v1) — the pinned SDK bundle
- [mpereiraesaa/prospero-win](https://github.com/mpereiraesaa/prospero-win) — the PS5 Wine runtime this is eventually aimed at
