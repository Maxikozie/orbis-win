# Milestones (our track on orbis-win)

Each milestone has one pass/fail check, decided before its first run, and every console launch gets one line in
`LOG.md`. Do not skip a milestone: each one tests what the next relies on.
- s0 is fixed.
- s1–s5 are drafts. Refine a milestone's check and write its `HANDOFF-<s>.md` before its first console run.
- After that, a check changes only with a reason recorded in the handoff.

Letters: `s` for this track, so nothing collides with ps4-wine's a–n (in `refs/ps4-wine/docs/ps4/MILESTONES.md`)
or with plastikman's package numbers (01.66, 02.16 ...).

| # | Milestone | Deliverables | Check | Main risk |
|---|---|---|---|---|
| s0 | Setup: this repo, refs, the `ps4` MCP over Remote Play | `~/orbis-win` (fork, `upstream` = plastikman), `refs/` (`scripts/rig/refs.sh`), `prospero-win/` on `plastikman/main`, CLAUDE.md, `scripts/rig/`, `~/git/ps4-mcp` (KDE Xwayland fix), chiaki-ng built and registered with the PS4, the MCP registered for this folder | In a session started in `~/orbis-win`: `/mcp` shows `ps4` connected, and `snapshot()` returns the PS4 home screen. Then `press("right")` + `snapshot()` shows the focus moved. LOG.md line 1 | Offline account and Remote Play pairing; KWin focus for `xdotool` |
| s1 | The release on the Slim | v0.0.7 (`IV0000-GLDS10043_00-GLDS100430000000.pkg`) put in `/data/pkg` over FTP and installed and launched by Claude over Remote Play. The data root GLDS10043 uses is recorded. Whether Remote Play keeps streaming while it runs is recorded | The launcher shows on HDMI (`shot.sh` frame plus a snapshot), and the session log or netlog carries the package's build marker (02.16) with no fault line | First run on a Slim; Remote Play forbidden while the app runs |
| s2 | Bloodlines on the Slim | The fork's PC-side Wine (`prospero-win/tools/build_host_wine.sh`, `build_wowprospero.sh`). A prefix with `bloodlines-up` (UP 11.5) and DXVK 2.6.2 x32. Profile from his `bloodlines-gog`, pushed with `pw_prefix.py push`. Fallback known-good game: FNV (`~/ps4-wine/games`) | New Game reaches a loaded level (Santa Monica), the user's word plus an HDMI frame, and the session log has no unhandled fault. ps4-wine's g target (`fps_avg >= 20`) is recorded, not gated | UP 11.5 vs plain GOG; the 26-minute host build; prefix push size |
| s3 | Psychonauts on orbis-win | A profile (xinput, `d3d9.maxFrameRate = 60`, from ps4-wine's k/l notes) and a prefix as in s2 | The checks of ps4-wine's k (menu with sound) and l (the DS4 as XInput, same bindings on a second launch), restated for orbis-win's logs. The user plays and compares with ps4-wine's launch 6 of l | Low base 0x400000 (ps4-wine's i); the pad path differs from ps4-wine's |
| s4 | Build from source on this laptop | The SDK bundle; Mesa + patches 0001–0044; the fork's Wine; the package as `GLDS10043`. Every machine-specific path we hit is fixed in a portable way: `/home/zpeirce`, OpenSSL 1.1, clang 19.1.7, `wow64native` ABI-4 | `scripts/check-ps4-package-parity.py <unpacked v0.0.7 stage> <our stage>` exits 0. Our package then repeats s1's check on the console | Exact clang 19.1.7; the private ABI-v3 `wow64native` path; a 26 GB CI cache we don't have |
| s5 | First pull requests to plastikman | s4's portability fixes (each its own branch from `upstream/main`). Then ports of ps4-wine wins, each with a console A/B in the two slots: thread stacks for every module (libvulkan), the 4.4 GiB package layout, Psychonauts' profile | Per PR: his gates pass (parity, patch presence, strings marker) and a GLDS10043-vs-GLDS10044 A/B line in LOG.md shows the change and no regression on Bloodlines | Divergence he must then carry (AGENTS.md: prefer upstream's code) |

Later, not scheduled:
- An MCP that takes video from the HDMI stick (leolobato's Linux hub in `refs/ps5-mcp-leo`, validated on the same
  MS2109).
- Input that needs no window focus.
- Text entry over Remote Play.
- Hollow Knight (plastikman's `hk/` branch).
