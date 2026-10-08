#!/usr/bin/env python3
"""What a Windows executable needs from the port, from its headers alone.

  scripts/pe-info.py <file.exe>...

Reports the three facts that decide whether a game can run here at all:

  arch           x86-64 runs native; i386 goes through the DBT (wowprospero).
  image base     where the image wants to live.
  relocations    stripped means it can live nowhere else -- and the console
                 loads this title at 0x400000-0x55c000, which it cannot be
                 moved out of (docs/FINDINGS.md). A 32-bit image based at
                 0x400000 with no relocations therefore cannot be loaded.

No Wine, no install, no console: this reads the PE headers and nothing more.
"""
import struct, sys

def info(path):
    with open(path, "rb") as fh:
        d = fh.read(0x600)
    if d[:2] != b"MZ":
        return "%s: not a PE" % path
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    if d[pe:pe+4] != b"PE\0\0":
        return "%s: no PE header" % path
    machine = struct.unpack_from("<H", d, pe + 4)[0]
    chars   = struct.unpack_from("<H", d, pe + 22)[0]
    magic   = struct.unpack_from("<H", d, pe + 24)[0]
    if magic == 0x20b:
        base = struct.unpack_from("<Q", d, pe + 24 + 24)[0]
        dll_chars = struct.unpack_from("<H", d, pe + 24 + 70)[0]
        dd = pe + 24 + 112
    else:
        base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        dll_chars = struct.unpack_from("<H", d, pe + 24 + 70)[0]
        dd = pe + 24 + 96
    reloc_rva, reloc_size = struct.unpack_from("<II", d, dd + 5 * 8)
    stripped = bool(chars & 0x0001)
    arch = {0x14c: "i386", 0x8664: "x86-64"}.get(machine, "0x%04x" % machine)

    if not stripped and reloc_size:
        verdict = "ok: relocatable, Wine can place it anywhere"
    elif arch == "x86-64":
        verdict = "ok: 64-bit, far above the title at 0x400000"
    elif base < 0x55c000 and base + 0x100000 > 0x400000:
        verdict = "BLOCKED: fixed at 0x%x, where this title is loaded" % base
    else:
        verdict = "probably ok: fixed, but clear of 0x400000-0x55c000"

    return ("%s\n  arch %-7s base 0x%-10x relocations %-13s large-address %s\n  %s"
            % (path, arch, base,
               "stripped" if stripped else ("%d bytes" % reloc_size if reloc_size else "none"),
               "yes" if chars & 0x0020 else "no", verdict))

if len(sys.argv) < 2:
    print(__doc__); raise SystemExit(2)
for p in sys.argv[1:]:
    try:
        print(info(p))
    except Exception as e:
        print("%s: %s" % (p, e))
