# orbis-win (Maxikozie's fork)

Wine for a jailbroken PS4 **Slim** (GoldHEN). This is plastikman's orbis-win, the PS4 port of prospero-win:
Wine's PE side, Unix side and wineserver in one PS4 title. Direct3D goes through DXVK 2.6.2 on mesa-ps4's RADV.
32-bit games run on native WoW64 by default, or on prospero-win's translator.

This checkout is our fork. `upstream` is plastikman/orbis-win, and our fixes go to him as pull requests.

Our targets on the Slim:
- Vampire: The Masquerade – Bloodlines (GOG + Unofficial Patch). It already plays on his Pro.
- Psychonauts (GOG).
- Then 32-bit D3D9 games in general.

Our predecessor, ps4-wine (milestones a–n, Psychonauts to l), is `refs/ps4-wine`: findings and code to take from
(see Licence). Its working tree, games and logs are in `~/ps4-wine`.

@AGENTS.md

`AGENTS.md`, imported above, is plastikman's project notes. They were written for Codex and bind here too.

## Read in this order

1. Ours:
   - `docs/milestones/MILESTONES.md`: s0–s5, one pass/fail check each.
   - `docs/milestones/LOG.md`: every console run.
   - The newest `docs/milestones/HANDOFF-*.md`.
2. plastikman's:
   - `README.md`: tested games, profiles.
   - `docs/INSTALLING-GAMES.md`: PC-side Wine, prefixes, profiles, pushing a game.
   - `docs/HANDOFF-2026-11-08.md`: his newest open list.
   - `docs/UPSTREAM-SYNC-PLAYBOOK.md`, `docs/FINDINGS.md`, `docs/HARDWARE-STATUS.md`.
3. ps4-wine's measurements: `refs/ps4-wine/CLAUDE.md` and `refs/ps4-wine/docs/ps4/`.

## Where the code is

| Part | Where |
|---|---|
| PS4 layer, packaging, gates, patch archives | here: `src/native`, `src/reloc-wine-app`, `src/*-patches-ps4`, `patches/`, `scripts/` |
| Wine patch series (to 1075), launcher, build tools | `prospero-win/` (gitignored), a clone of Maxikozie/prospero-win. Branch `ps4` tracks `plastikman/main`. Remotes: `plastikman`, `upstream` (mpereiraesaa), `leo` (leolobato). Our old branch `orbis` is ps4-wine's fork |
| Mesa | orbis-ports/mesa-ps4 `1a59cc1` + `src/mesa-patches-ps4` 0001–0044, built in `mesa-build/` (gitignored) |
| SDK bundle | `sdk/orbis-sdk-v1`, fetched by `scripts/fetch-sdk.sh` (orbis-porting-kit release; OpenOrbis v0.5.4 inside) |
| ps4-mcp (Remote Play MCP server) | `~/git/ps4-mcp`, a separate repo (AGPL-3.0). Branch `kde-xwayland` (our KDE fixes) on plastikman's `ps4-remoteplay`. Remotes: `origin` = Maxikozie/ps4-mcp, `upstream` = plastikman, `leo` = leolobato/ps5-mcp |
| References (read-only) | `refs/`, listed and kept current by `scripts/rig/refs.sh`. nx-on-orbis is GPL: facts only |

## Hard rules

- **Licence.** This repo is LGPL-2.1-or-later (`LICENSE`, `LICENSING.md`).
  - ps4-wine is GPL-3.0. From it, only code its author wrote (Maxikozie, with Claude) comes over, relicensed
    LGPL-2.1-or-later by that author.
  - Never bring over code ps4-wine took from nx-on-orbis (GPL-3.0) or orbis-compat's `orbis-tls.ld`
    (GPL-3.0-only). Facts and measured offsets from any source are fine.
  - Every reused block carries the comment `From: <repo> <path> @ <commit> (<licence>)`.
- **Upstream PRs.** A fix for plastikman, or for his prospero-win fork, follows these rules:
  - It is a branch cut from `upstream/main` (`plastikman/main` in `prospero-win/`), with one topic.
  - It follows his conventions: AGENTS.md, and patch numbers above his highest.
  - It never carries `CLAUDE.md`, `docs/milestones/`, `scripts/rig/` or our console IP. Those live only on our
    `main`.
  - Opening a PR is the user's call.
- **Inputs.** OpenOrbis and the orbis-porting-kit bundle only, never a leaked Sony SDK.
  - Nothing tracked from game files, `.exe`/`.dll` files or `sce_module/`.
  - GoldHEN is a given: no exploit work, and title code never depends on firmware-specific kernel offsets.
- **Languages.** C for runtime code, in Wine's style. Bash for tooling, with `set -euo pipefail` and `--help` on
  every script. Python 3 stdlib only, where bash is not enough.
- **Evidence.**
  - Host checks first. One console build should test several hypotheses at once.
  - Before a console test, prove the change is in the shipped binary. The build does not recompile changed sources
    (AGENTS.md), so check with `strings <module> | grep -c <marker>`.
  - Every console launch appends exactly one line to `docs/milestones/LOG.md`.
  - Say plainly which console behaviour was not tested.
  - Never edit a hash, a pin, a parity baseline or a threshold to make a check pass.
- **Division of labour.**
  - Claude builds, packages and deploys over FTP. Through the `ps4` MCP (Remote Play), Claude installs packages in
    GoldHEN's Package Installer, launches and closes titles, navigates menus and takes snapshots. Claude also reads
    the logs.
  - The user plays and judges games, and does what Remote Play cannot (sign-ins, GoldHEN settings). Ask the user
    before anything that deletes data on the console.
  - The laptop is the user's only PS4 screen.
    - Open `scripts/rig/view.sh` unasked whenever the user is to play or watch: in the background, with the
      "PS4 Capture" entry's arguments (its `Exec` line in `~/.local/share/applications/ps4-capture.desktop`, i.e.
      the HDMI `--sink`).
    - Reopen it right after every capture that took the stick (`shot.sh`, a recording).
- **Git.** The user commits. Do not commit or push unless asked. Never edit anything under `refs/`. Only
  `scripts/rig/refs.sh --update` moves it, and only when asked.

## Environment

| What | Value |
|---|---|
| PS4 Slim | `192.168.8.184` (`config.env`, gitignored), GoldHEN. FTP `2121` serves the real root. plastikman's `scripts/ftp-*.py` log in to nothing, because lftp's login crashed his GoldHEN FTP; `curl` and `lftp -c 'open ...'` worked for ps4-wine. klog TCP `3232`: a plain stream, no history, one client. BinLoader `9090`: one connection per manual enable |
| Logs | netlog UDP `18194` from the title (`scripts/logs.sh`). App log TCP `9300` (`scripts/ps5logd.py`, when the package's `dev.conf` points here). Session logs in `/data/prospero-win/logs/` (`scripts/pull-ps5log.py`, `scripts/ftp-get.py`). klog (`scripts/watch-klog.sh`). All land in `build-ps4-logs/` (gitignored) |
| Titles | Release and candidate slot `GLDS10043`, installer `IV0000-GLDS10043_00-GLDS100430000000.pkg` (v0.0.7 = runtime 02.16). Control/baseline slot `GLDS10044`. ps4-wine's `PSWN00001` (`/data/ps4-wine`) may still be installed |
| Console paths | Packages in `/data/pkg/`. Runtime data in `/data/prospero-win/{profiles,input,prefixes/<profile>,logs,settings.conf}`. From v0.0.5 the runtime picks its data root by title ID: s1 records which one GLDS10043 uses |
| Host | Kubuntu 26.04 (KDE Plasma, Wayland with Xwayland `:0`), x86_64, btrfs `/home`. OpenOrbis v0.5.4 at `~/.local/opt/openorbis`. Private OpenSSL 1.1 at `~/.local/opt/openssl-1.1` (orbis-win expects `~/.cache/orbis-ports/openssl11/lib`). The system clang is newer than the exact 19.1.7 that his release builds pin (s4) |
| Games | Read-only, in `~/ps4-wine/games/`: `bloodlines-gog`, `bloodlines-up` (UP 11.5), `psychonauts`, Fallout New Vegas Ultimate, GTA IV, Pinball |
| Scratch | `$SCRATCH_ROOT` = `~/orbis-win/build/scratch` (AGENTS.md: never `/tmp` for project artifacts) |
| Screen | USB HDMI capture stick: MacroSilicon MS2109 (USB `345f:2109`), MJPEG 1920x1080 up to 60 fps. PS4 at 1080p, HDCP off. No passthrough: the TV stays dark. The `/dev/videoN` numbers change between boots (2026-11-16: stick `video0`, webcam `video2`); `shot.sh` finds the stick by name and `view.sh` by its `/dev/v4l/by-id` path. Only one program can read it at a time: close the viewer (its `pgrep -x` PID, never `pkill -f`) before `shot.sh` or any capture. After a stream opens it sends black (7,7,7) for ~1.7 s, so `shot.sh` keeps frame 150; an all-7,7,7 frame means no picture or no signal. For short-lived pictures, record through the viewer (`view.sh --sink ... --record build-ps4-logs/<ts>-X.mkv`, ~0.6 GB a minute, stops after `--record-max` s; `--stop-record` ends it early). Then cut a frame with `ffmpeg -ss T -i X.mkv -frames:v 1 F.png`, keep a few seconds around it and delete the rest |
| Remote Play | chiaki-ng, pinned as `~/git/ps4-mcp/third-party/chiaki-ng`, built to `~/git/ps4-mcp/build/linux-chiaki/gui/chiaki`. Registered with the console's offline-activated account; the Slim streams 720p. The `ps4` MCP server is registered for this folder with local scope (`claude mcp get ps4`), which overrides the tracked `.mcp.json` (plastikman's IP) |

## Scripts

plastikman's are in `scripts/`; ours are in `scripts/rig/`.

| Script | Does |
|---|---|
| `scripts/preflight.sh`, `wait-for-console.sh` | console reachable, which port serves FTP, UDP 18194 free (it also asks for a triangle package: ignore that) |
| `scripts/logs.sh` | netlog (UDP 18194) through the bundle's receiver, or `klog`; refuses a second klog client |
| `scripts/watch-klog.sh`, `klog-alive.sh` | klog into `build-ps4-logs/` with reconnects; did it grow in N s |
| `scripts/ps5logd.py`, `pull-ps5log.py` | the app's live log on TCP 9300; the session logs over FTP |
| `scripts/watch-coredump.py HOST PORT KLOG OUT TITLE_ID` | fetches a `.sorbisdmp` before GoldHEN deletes it. Run it from the repo root. Dumps are encrypted: keep them, don't decode them (AGENTS.md) |
| `scripts/ftp-put.py`, `ftp-get.py`, `ftp-put-tree.py`, `ftp-*.py`; `deploy.sh` | FTP without login; `.pkg` to `/data/pkg/` |
| `scripts/fetch-sdk.sh`, `prereqs.sh`, `prepare-ps4-toolchain.sh` | the SDK bundle; host tools check; the build shim. `sync-toolchain.sh:5` hardcodes `/home/zpeirce/ps4-gladius` (s4) |
| `scripts/prx-ps4.sh`, `radv-prx-ps4.sh`, `freetype-prx-ps4.sh`, `wineps5-prx-ps4.sh`, `build-relocatable-wine-ps4.sh` | Wine's Unix side and RADV as PRXs; the package (default `GLDS10044`; `TITLE_ID=GLDS10043`). `docs/BUILDING.md` is out of date (s4) |
| `scripts/check-ps4-package-parity.py`, `audit-patch-presence.py`, `stage-provenance.py` | the gates (AGENTS.md); the baseline is an unpacked release stage |
| `prospero-win/tools/build_host_wine.sh`, `build_wowprospero.sh`, `pw_prefix.py push` | the PC-side Wine, prefixes, pushing a game (`docs/INSTALLING-GAMES.md`) |
| `scripts/rig/refs.sh` | `refs/`: `--status` (default), `--copy-from DIR`, `--clone`, `--update` (fast-forward only) |
| `scripts/rig/view.sh` | the user's live view: mpv shows the stick, `pw-loopback` plays its sound. Holds the stick while open. `--record FILE`, `--record-max`, `--stop-record` |
| `scripts/rig/shot.sh` | one frame from the stick (pinned MJPEG 1920x1080); `--pixel X,Y`, `--expect-rgb` |

The loop for one console run:
1. Start `scripts/watch-klog.sh &` and `scripts/logs.sh &` first: klog has no history.
2. Put the package on the console with `ftp-put.py` (to `/data/pkg/`).
3. Install it through Remote Play (GoldHEN's Package Installer).
4. Start `view.sh &` if the user is to watch or play.
5. Launch through Remote Play.
6. Read the logs; take a snapshot (MCP) or, for an exact HDMI pixel, `shot.sh`.
7. Close the title, unless the user still plays.
8. Append one line to `docs/milestones/LOG.md`.

## Remote Play and the `ps4` MCP

- **The connection.** chiaki-ng is the Remote Play client.
  - Snapshots are `xwd` grabs of its window (720p on the Slim; exact HDMI pixels need `shot.sh`).
  - Buttons are `xdotool` keys sent to its "keyboard as controller".
  - The server starts chiaki-ng when it is not running. A session must then be connected in its window.
- **Tools.**
  - Working: `snapshot`, `status`, `press`, `hold`, `sequence`, `stick` (digital directions only), `ps_button`,
    `home`, `release_all`.
  - Not on this backend: text entry, analog magnitude, triggers as analog, touchpad, and the PS5 title list and
    `launch`. Launch titles from the home screen instead.
- **Focus.** Every input activates chiaki's window, so the user should not type while Claude presses buttons.
- **Measured 2026-11-16** (through the adapter, before the first MCP session):
  - Presses land: 8/8 with a 150 ms hold.
  - The picture shows the result 0.4–1.3 s after the press starts. Take a snapshot at least 1.5 s after an input
    (`snapshot_after_ms=1500`). A snapshot taken earlier can still show the old screen and look like a missed press.
- **Pairing.** Remote Play is registered to the PS4 user `maxikozie`, account ID `0x1111111111111111`
  (Base64 `ERERERERERE=`). Apollo lists that account as "VueUser", already activated.
  - Do not activate it again in Apollo: a new ID could stop Vue, the jailbreak entry point.
- **KDE fixes** (ps4-mcp branch `kde-xwayland`):
  - The display is found through KWin's `xauth_*` file.
  - chiaki's window is found by WM_CLASS, because KDE's Xwayland leaves its `WM_NAME` empty.
- **Not tested yet:**
  - whether Remote Play keeps working while GLDS10043 runs (param.sfo attributes can forbid it);
  - whether Remote Play pad input reaches the title's pad code (`src/native/pw_pad_ps4.c`) as a normal pad;
  - what Remote Play costs a running game in frame rate.

  Record each in LOG.md when first seen.

## PS4 traps

These were measured by orbis-ports, nx-on-orbis, playden, ps4-wine and orbis-win. "ps4-wine:" marks a fact from
our old port; "orbis-win:" marks how this repo differs.

- **The SDK's headers.** The SDK ships musl headers over Sony's FreeBSD-9-derived libc, so `struct stat`
  (`mode_t`), pthread attr sizes and `bits/signal.h` are wrong. Build with an orbis-compat overlay ahead of the SDK
  (orbis-win: `src/freebsd-compat`, the bundle's orbis-compat).
- **Syscalls and the file system:**
  - A raw `syscall` instruction outside libkernel kills the process. libc `syscall()` returns ENOSYS.
  - Relative `open` is EINVAL, `getcwd` ENOSYS, `fchdir` crashes, and `openat` takes only `AT_FDCWD`: use absolute
    paths.
  - `dup` is EPERM and `fcntl(F_DUPFD)` EINVAL on every fd. `close`/`dup2` on fds 0-2 are EPERM.
  - `pipe`, `socketpair`, `SCM_RIGHTS` fd passing and `shm_open(SHM_ANON)` work. Passing a descriptor to the
    process itself with SCM_RIGHTS duplicates it.
  - `sceKernelStat` by path returns dev/ino 0; `_fstat` on a descriptor returns them. Files under `/data` are root's.
  - `realpath()` fails for every path (musl's `readlink` of `/proc/self/fd/N` is an ENOSYS stub).
- **Thread stacks.**
  - The default thread stack is 64 KiB. orbis-compat's `pthread_create` (one copy per module) raises smaller
    requests to a floor taken from the first creating thread's stack: 16 MiB in ntdll's and libvulkan's.
  - A 16 MiB libkernel stack costs 16 MiB of flexible memory and is not returned to the pool.
  - ps4-wine fixed this at the orbis-compat level, for every module: `refs/ps4-wine/patches/orbis-compat/0001`, with
    `WINE_PS5_THREAD_STACK=keep|small|floor`.
  - orbis-win wraps `pthread_create` in ntdll and wineserver only (`src/native/pw_pthread_stack_ps4.c`,
    `docs/WINESERVER-STACK-PS4.md`: 1 MiB, retry at 512 KiB). libvulkan's threads are not covered.
  - `timer_create` with SIGEV_THREAD hangs.
  - `sigaltstack` works with FreeBSD's `stack_t` (`SS_DISABLE` 0x4). The SDK's musl header swaps
    `ss_size`/`ss_flags`.
- **Exit.** Returning from `main()` or calling `exit()` is reported as CE-34878-0, and writes to fd 1/2 reach only
  klog. orbis-win's games quit back to the launcher by restarting the app.
  - Close a running title before reinstalling it: installing over it fails with CE-33237-8.
- **The released eboot (no RELRO).** After the eboot is released, the kernel's module loader still re-protects
  module 0's `PT_SCE_RELRO` on every module load. libkernel's `sceKernelGetModuleList` also reads module 0's data.
  - So the stub has no `PT_SCE_RELRO` and keeps its data at 0x7fff0000; only the code page at 0x400000 is released.
  - orbis-win took this from ps4-wine: `docs/NO-RELRO-EBOOT-PS4.md`, `src/reloc-wine-app/stub-no-relro.ld`,
    `scripts/prepare-orbis-stub-no-relro.py`.
  - `sceKernelVirtualQuery` shows direct-memory RX as prot 1 and RWX as 3.
- **Executable memory.** `sceKernelAllocateDirectMemory`, map RW, then `sceKernelMprotect` RWX. Asking for EXEC at
  map time fails. The page granule is 16 KiB.
- **Memory budgets.**
  - Flexible memory (`mmap(MAP_ANON)`, libc malloc) is ~384–440 MiB.
  - ps4-wine: direct memory is ~4.4 GiB **only** with the OpenOrbis-samples package layout: `sce_module/libc.prx` +
    `libSceFios2.prx`, `sce_sys/about/right.sprx`, SFO `CATEGORY gd`, `SYSTEM_VER 0`, paid `0x3800000000000011`.
    Other layouts give 768 MiB or do not boot.
  - orbis-win sends Wine's anonymous memory to direct memory (`pw_wine_dmem_ps4.c`; the wineserver heap with
    `PW_WINESERVER_HEAP_DIRECT=1`). Whether its package gets the 4.4 GiB layout is unmeasured; its logs show
    0.8–1.45 GiB in use.
  - `sceKernelReserveVirtualRange`: 16 GiB anywhere, 512 GiB FIXED at `0x1000000000`.
- **Faults.** They arrive through `sceKernelInstallExceptionHandler` or FreeBSD `sigaction`, with the PS5's frame:
  mcontext at ucontext+64 (rip +224), FXSAVE +320, FS base +1152, cs64 `0x43`. `sysarch` 128–131 get/set FS and GS.
  A far jump to `0x33` runs 32-bit code.
  - Guest faults: AGENTS.md's `PW_EXCEPTION32` records need the 32-bit `kernelbase.dll` built with them.
- **App PRXs:**
  - `sceSystemServiceLoadExec(path, args)` restarts the title.
  - An app PRX loads and `sceKernelDlsym` resolves its exports. The loader runs neither constructors nor
    `module_start`.
  - One app PRX imports from another only if the exporter is already loaded and the sonames agree.
  - A PT_LOAD that starts mid-page is ENOEXEC.
  - Each PRX has its own musl: `environ` and `stderr` are per module.
- **libc.** `libc.a` has a 16-bit `wchar_t` and 134 functions that only set ENOSYS: `epoll_*`, `ppoll`,
  `readlink`, the `*at` family, `getrandom` and more. libkernel has `kqueue`/`kevent`/`poll`.
  - klog (`sceKernelDebugOutText`) costs 8–15 ms a line and blocks when nothing drains it. Use netlog for volume.
- **Vulkan** (RADV, "PlayStation 4 Liverpool", GFX7, Vulkan 1.3):
  - A submission must fit 2 MiB or the GPU hangs. orbis-win has no splitting patch for this.
  - `VK_EXT_conditional_rendering` and `VK_KHR_draw_indirect_count` hang the GPU.
  - About one submit in 1200 stalls.
  - Surface formats are RGBA only.
  - Releases run in base (Liverpool) mode even on a Pro; `--neo` / `ORBIS_FORCE_NEO` are Pro experiments. Never
    use them on the Slim.
- **CPU.** Compile with `-march=btver2 -femulated-tls -fno-omit-frame-pointer`. The cpuset is `0x3f`: 6 Jaguar
  cores at 1.59 GHz on the Slim.
