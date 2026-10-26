#!/usr/bin/env python3
"""Create an isolated Fallout 3 prefix from the validated Bethesda prefix base.

The Wine Windows tree and PS4 registry hives come from the New Vegas prefix
stage, while Fallout 3 receives its own Documents/AppData settings and install
path registration. This never edits the Fallout NV stage or the Windows drive.
"""
from __future__ import annotations

from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build/falloutnv-prefix"
OUTPUT = ROOT / "build/fallout3-gog-prefix"
CONFIG = ROOT / "src/fallout3-config"


def main() -> int:
    if not SOURCE.is_dir():
        raise SystemExit(f"missing validated prefix base: {SOURCE}")
    if OUTPUT.exists():
        raise SystemExit(f"refusing to overwrite existing prefix: {OUTPUT}")
    for name in ("Fallout.ini", "FalloutPrefs.ini"):
        if not (CONFIG / name).is_file():
            raise SystemExit(f"missing Fallout 3 config: {CONFIG / name}")

    shutil.copytree(SOURCE, OUTPUT, copy_function=shutil.copy2)
    user = OUTPUT / "drive_c/users/prospero"
    shutil.rmtree(user / "Documents/My Games/FalloutNV", ignore_errors=True)
    shutil.rmtree(user / "AppData/Local/FalloutNV", ignore_errors=True)

    docs = user / "Documents/My Games/Fallout3"
    (docs / "Saves").mkdir(parents=True)
    local = user / "AppData/Local/Fallout3"
    local.mkdir(parents=True)
    for name in ("Fallout.ini", "FalloutPrefs.ini"):
        shutil.copy2(CONFIG / name, docs / name)

    # GOG's offline installer writes this exact registration. The game is
    # launched directly from its working directory, but retaining the proper
    # Installed Path also lets any game code that consults the key find Data.
    registry = OUTPUT / "system.reg"
    text = registry.read_text(encoding="utf-8")
    marker = "[Software\\\\Bethesda Softworks\\\\Fallout3]"
    if marker in text:
        raise SystemExit("Fallout 3 registry key already exists in source prefix")
    text += (
        "\n[Software\\\\Bethesda Softworks\\\\Fallout3] 1791417780\n"
        "#time=1dd56b8619811d4\n"
        '"Installed Path"="C:\\\\Games\\\\Fallout3\\\\"\n'
    )
    registry.write_text(text, encoding="utf-8")

    (OUTPUT / "drive_c/Games/Fallout3").mkdir(parents=True, exist_ok=True)
    print(f"Created isolated prefix: {OUTPUT}")
    print(f"Wine files: {sum(1 for p in OUTPUT.rglob('*') if p.is_file())}")
    print(f"Settings: {docs}")
    print("GOG Fallout3 Installed Path registered in system.reg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
