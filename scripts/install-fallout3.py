#!/usr/bin/env python3
"""Upload the GOG Fallout 3 game, isolated prefix, and profile to GoldHEN.

No FTP login, LIST, SIZE, or filesystem probes are used. Directories are
created for the transfer and files are sent with STOR. Completed files are
tracked under ~/codex_scratch so a dropped transfer can resume safely.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import socket
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
GAME_SOURCE = Path.home() / "codex_scratch/fallout3-gog-extract/app"
PREFIX_SOURCE = ROOT / "build/fallout3-gog-prefix"
DXVK_D3D9 = Path.home() / "codex_scratch/fallout3/d3d9.dll"
PROFILE = ROOT / "src/profiles/fallout3-gog.profile"
STATE = Path.home() / "codex_scratch/fallout3/upload-state.json"
REMOTE_PREFIX = "/data/prospero-win/prefixes/fallout3-gog"
REMOTE_PROFILE = "/data/prospero-win/profiles/fallout3-gog.profile"

EXCLUDED_ROOT_NAMES = {
    "__support", "F3icon.ico", "GECK.exe", "GECKicon.ico",
    "GECKReadme.txt", "Readme.txt", "goggame-1454315831.hashdb",
    "goggame-1454315831.info", "goggame-1454315831.script", "webcache.zip",
}


def load_goldhen_ftp():
    spec = importlib.util.spec_from_file_location(
        "falloutnv_installer", ROOT / "scripts/install-falloutnv.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load the known-safe GoldHEN FTP client")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.GoldHENFTP, module.FTPError


def selected_game_files() -> list[Path]:
    files = []
    for path in GAME_SOURCE.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(GAME_SOURCE)
        if rel.parts[0] in EXCLUDED_ROOT_NAMES:
            continue
        files.append(path)
    if not any(p.name == "Fallout3.exe" and p.parent == GAME_SOURCE for p in files):
        raise RuntimeError(f"GOG game executable missing under {GAME_SOURCE}")
    return sorted(files)


def save_state(path: Path, completed: dict[str, dict[str, int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".new")
    temp.write_text(json.dumps({"completed": completed}, sort_keys=True))
    temp.replace(path)


def transfer(client, local: Path, remote: str, size: int) -> None:
    reply = client.command("PASV")
    if not reply.startswith("227"):
        raise RuntimeError(f"PASV: {reply}")
    fields = reply[reply.index("(") + 1:reply.index(")")].split(",")
    host = ".".join(fields[:4])
    port = int(fields[4]) * 256 + int(fields[5])
    data = socket.create_connection((host, port), timeout=30)
    sent = 0
    next_report = 64 * 1024 * 1024
    try:
        client.command(f"STOR {remote}", ("125", "150"))
        with local.open("rb") as stream:
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                data.sendall(block)
                sent += len(block)
                if sent >= next_report:
                    print(f"    {sent * 100 // max(size, 1)}% of {local.name}",
                          flush=True)
                    next_report += 64 * 1024 * 1024
    finally:
        data.close()
    if sent != size:
        raise RuntimeError(f"local file changed during transfer: {local}")
    reply = client.read_reply()
    if not reply.startswith("226"):
        raise RuntimeError(f"STOR {remote}: {reply}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=2121)
    parser.add_argument("--state", type=Path, default=STATE)
    args = parser.parse_args()
    for path in (GAME_SOURCE, PREFIX_SOURCE, DXVK_D3D9, PROFILE):
        if not path.exists():
            parser.error(f"required install input is missing: {path}")

    GoldHENFTP, FTPError = load_goldhen_ftp()
    game_files = selected_game_files()
    payload: list[tuple[Path, str]] = []
    for path in PREFIX_SOURCE.rglob("*"):
        if path.is_symlink():
            parser.error(f"prefix contains unsupported symlink: {path}")
        if path.is_file():
            payload.append((path, f"{REMOTE_PREFIX}/{path.relative_to(PREFIX_SOURCE).as_posix()}"))
    game_remote = f"{REMOTE_PREFIX}/drive_c/Games/Fallout3"
    payload.extend((path, f"{game_remote}/{path.relative_to(GAME_SOURCE).as_posix()}")
                   for path in game_files)
    payload.append((DXVK_D3D9, f"{game_remote}/d3d9.dll"))
    payload.append((PROFILE, REMOTE_PROFILE))

    try:
        completed = json.loads(args.state.read_text()).get("completed", {})
    except FileNotFoundError:
        completed = {}
    except (OSError, json.JSONDecodeError) as exc:
        parser.error(f"cannot read transfer state {args.state}: {exc}")

    directories = {"/data/prospero-win", "/data/prospero-win/prefixes",
                   "/data/prospero-win/profiles", REMOTE_PREFIX,
                   f"{REMOTE_PREFIX}/drive_c", f"{REMOTE_PREFIX}/drive_c/Games",
                   game_remote}
    for path in PREFIX_SOURCE.rglob("*"):
        if path.is_dir():
            directories.add(f"{REMOTE_PREFIX}/{path.relative_to(PREFIX_SOURCE).as_posix()}")
    for path in GAME_SOURCE.rglob("*"):
        if path.is_dir() and path.relative_to(GAME_SOURCE).parts[0] not in EXCLUDED_ROOT_NAMES:
            directories.add(f"{game_remote}/{path.relative_to(GAME_SOURCE).as_posix()}")

    total_bytes = sum(local.stat().st_size for local, _ in payload)
    pending = []
    sent_bytes = 0
    for local, remote in payload:
        stat = local.stat()
        key = remote
        record = completed.get(key)
        if record == {"size": stat.st_size, "mtime_ns": stat.st_mtime_ns}:
            sent_bytes += stat.st_size
        else:
            pending.append((local, remote, stat.st_size, stat.st_mtime_ns))
    print(f"Files: {len(payload)}, pending: {len(pending)}, "
          f"total: {total_bytes / 2**30:.2f} GiB", flush=True)

    client = None
    try:
        client = GoldHENFTP(args.host, args.port)
        for directory in sorted(directories, key=lambda p: (p.count("/"), p)):
            client.mkdir(directory)
        last_report = time.monotonic()
        for index, (local, remote, size, mtime_ns) in enumerate(pending, 1):
            print(f"[{index}/{len(pending)}] {remote} ({size} bytes)", flush=True)
            transfer(client, local, remote, size)
            completed[remote] = {"size": size, "mtime_ns": mtime_ns}
            sent_bytes += size
            if index == len(pending) or index % 10 == 0 or time.monotonic() - last_report > 10:
                save_state(args.state, completed)
                last_report = time.monotonic()
                print(f"Progress: {sent_bytes * 100 // max(total_bytes, 1)}% "
                      f"({sent_bytes / 2**30:.2f}/{total_bytes / 2**30:.2f} GiB)",
                      flush=True)
        save_state(args.state, completed)
    except (OSError, RuntimeError, FTPError, socket.timeout) as exc:
        save_state(args.state, completed)
        print(f"Fallout 3 upload stopped: {exc}", file=sys.stderr)
        return 1
    finally:
        if client:
            client.close()
    print("Fallout 3 game/prefix uploaded; profile was sent last.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
