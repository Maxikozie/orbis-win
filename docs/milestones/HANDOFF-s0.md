# Handoff s0: setup (2026-11-16)

The session that set up this repo wrote this file. s0's check (MILESTONES.md) is still owed: the first session in
`~/orbis-win` runs it through the `ps4` MCP and writes LOG.md line 1.

## What exists

- **This repo.** A clone of Maxikozie/orbis-win with `upstream` = plastikman/orbis-win. `main` = upstream's
  `9f6619f` + our process files: `CLAUDE.md`, `docs/milestones/`, `scripts/rig/`.
- **`refs/`.** 16 read-only checkouts, all equal to their remotes on 2026-11-16 (`scripts/rig/refs.sh`).
  `refs/ps4-wine` is at `34eca8f` (the "parked" note).
- **`prospero-win/`.** Branch `ps4` = `plastikman/main` `fe5c8d1`, 40 commits ahead of upstream `18115ce`.
  Nothing is built yet: no SDK bundle (`sdk/`), no `mesa-build/`, no host Wine.
- **`~/git/ps4-mcp`.** Branch `kde-xwayland` on plastikman's `ps4-remoteplay`, pushed to Maxikozie/ps4-mcp
  (remote `origin`; plastikman's is `upstream`, leolobato's is `leo`).
  - `.venv` with the package and pytest.
  - chiaki-ng built at `build/linux-chiaki/gui/chiaki`, from the pinned submodule `c8bea264`.
- **The `ps4` MCP.** Registered for this folder with local scope:
  `claude mcp get ps4` shows the PS4's IP in its env (`PS4_HOST=192.168.8.184`).
- **Memories.** The general ones from ps4-wine are copied to this project's memory directory.

## Measured this session

- **Remote Play pairing.** chiaki-ng is registered with the PS4 ("PS4-470", firmware ≥ 8.0 profile).
  - The signed-in user is `maxikozie`, account ID `0x1111111111111111`, which Apollo lists as "VueUser",
    already activated.
  - Base64 for chiaki: `ERERERERERE=`.
  - Do not activate that account again: Vue (the jailbreak entry) may depend on that ID.
- **Through the adapter's own code** (`LinuxChiakiApp` + `make_client`, not yet through the MCP server):
  - Snapshots are 1280x720 `xwd` grabs of chiaki's window.
  - 8 of 8 D-pad presses landed with a 150 ms hold. Their result appears 0.4–1.3 s after the press starts.
  - A single grab 1.0 s after a press showed the old screen 1–2 times in 6. Those looked like misses but were not.
- **KDE needed two fixes** in `src/ps5mcp/linux_chiaki.py`:
  - Find the Xwayland auth file `xauth_*` (KWin), not only Mutter's.
  - Find chiaki's window by `WM_CLASS` `^chiaki$`: under KDE's Xwayland its `WM_NAME` is empty, and xdotool's
    `--name` reads only that.
- **ps4-mcp's tests on Linux:** 125 pass. 17 fail and 166 error, all in tests that build the macOS app (`swiftc`) or
  the PS5 Remote Play setup payload (darwin flags). No test covers `linux_chiaki.py`.
- **The home screen reset (the PS4's `app.db`):** it happened on 2026-11-04 ~15:53 console time; the user noticed
  today.
  - Every app stayed on disk. 34 were re-registered from their `param.sfo`, and the user confirmed after a restart.
  - Tool: `build/scratch/appdb/rebuild_appdb.py`. The console's backup is in `/data/appdb-backup/`. See the memory
    `ps4-appdb-rebuild`.
- **The PS4's clock is about an hour behind the laptop**: 15:57 on the console at about 16:55 here. Correlate logs
  with that in mind.
- **The home screen shows each title's ID under its tile.** It is a GoldHEN or Itemzflow setting, and it lets a
  snapshot tell titles apart.

## Not done, not tested

- s0's check through the MCP server itself (`snapshot`, `press`) in a session started here.
- Which MCP tools work on the Linux backend beyond those the adapter implements:
  - `wait_for` / `save_template` / `wait_for_change` / recording ride on `capture` code made for the macOS app or
    the ffmpeg hub;
  - `launch`, `list_apps`, `install`, `close_app`, `type_text` are PS5 payload paths and raise errors here.
- Installing a package through GoldHEN's Package Installer over Remote Play: the button path is not recorded.
- Whether Remote Play keeps streaming while an orbis-win title runs, and whether its pad reaches the title.
- orbis-win's own log tools: `scripts/logs.sh` wraps the SDK bundle's receiver, so it needs `scripts/fetch-sdk.sh`
  first. `ps5logd.py` needs the package's `dev.conf` to point at this host.
- How much free space `/data` has on the console. GoldHEN's FTP listing does not show it.

## Rough edges to fix before s1 (the user wants none left)

1. **Console control is not packaged as one tool.** The MCP lacks PS4 versions of `home`, `close_app`, launch-a-title
   and install-a-package. Each is a button sequence plus a snapshot check: tile labels carry title IDs; Close
   Application is in the quick menu. Write and test them once, then reuse them.
2. **Snapshot timing.** Every check after an input must wait ≥ 1.5 s or poll until the picture changes. Bake that into
   the tools or the loop, not into memory.
3. **chiaki-ng's lifecycle.** It must be running, connected and on the stream. After a PS4 restart or rest mode the
   session drops. Needed:
   - a check (is the window streaming the console or showing the host list?);
   - a reconnect routine;
   - possibly autostart.
4. **Focus.** Every MCP input raises chiaki's window and takes the keyboard: the user must not type meanwhile.
   Acceptable for now. Note it in the loop so it never surprises the user.
5. **One preflight for the whole rig.** Console ping, FTP, GoldHEN loaded, chiaki streaming, the `ps4` MCP
   connected, capture stick present (viewer open or not), `config.env`, the log tools ready. One script, one line of
   output, run before every console session.
6. **The run loop as a script.** Logs started under `systemd-inhibit` before anything, deploy, install, launch,
   capture, close, and the LOG.md line printed for the user to approve. Today these are separate manual steps
   (CLAUDE.md "The loop").
7. **Guard `app.db`.** Back it up (the rebuild script's `fetch`) before every install session, given 2026-11-04.
8. **Clean up `.mcp.json`.** plastikman's tracked `.mcp.json` (his IP) and our local-scope `ps4` share a name, so a
   session may ask to approve his.
   - Prepare a small upstream PR: `PS4_HOST` and the ps4-mcp path from the environment.
   - Prepare a ps4-mcp PR with the two KDE fixes and a Linux test for them.
9. **`sudo` cannot run inside the Claude app** (it runs with `NoNewPrivs`, so every shell it starts does too). Every
   apt install is a command the user runs in Konsole. Collect all host packages s1–s4 will need (s4's exact
   clang 19.1.7, the SDK bundle's tools) into one list, so the user installs once.
10. **The two console clocks.** Either the user sets the PS4's time, or the logs carry the measured offset.
