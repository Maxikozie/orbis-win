#!/usr/bin/env python3
"""Apply a narrowly version-locked null-vtable guard to FalloutNV.exe."""
import argparse
import hashlib
import struct
from pathlib import Path

EXPECTED_SHA256 = "a50c0e91b5d729ef10a921c32aa0cc20aac5366630056d1d60c669917a936fbb"
IMAGE_BASE = 0x400000
TARGET_RVA = 0x659D30
ORIGINAL = bytes.fromhex(
    "538b5c240883fb077c0633c05bc20400568b71245785f67427"
    "8da424000000008b7e088d46088b3685ff74108b078b908c000000"
    "8bcfffd23bc3740c85f675e05f5e33c05bc204008bc75f5e5bc20400"
)
PATCH = bytes.fromhex(
    "5356578b5c241083fb077c288b712485f674218b7e088b3685ff"
    "74f38b0785c074ed8b908c00000089f9ffd239d875df89f8eb02"
    "31c05f5e5bc20400"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def target_file_offset(data: bytes) -> int:
    if data[:2] != b"MZ":
        raise ValueError("input is not a PE file (missing MZ signature)")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("input has an invalid PE signature")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x10B:
        raise ValueError("expected a 32-bit PE32 executable")
    if struct.unpack_from("<I", data, optional + 28)[0] != IMAGE_BASE:
        raise ValueError("unexpected PE image base")
    section_table = optional + optional_size
    for i in range(section_count):
        off = section_table + i * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, off + 8)
        extent = max(virtual_size, raw_size)
        if virtual_address <= TARGET_RVA < virtual_address + extent:
            raw_offset = raw_pointer + TARGET_RVA - virtual_address
            if TARGET_RVA + len(ORIGINAL) > virtual_address + raw_size:
                raise ValueError("target function is not fully backed by section file data")
            return raw_offset
    raise ValueError(f"RVA 0x{TARGET_RVA:x} is not in a PE section")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="unmodified FalloutNV.exe")
    parser.add_argument("output", type=Path, help="new patched executable path")
    args = parser.parse_args()
    source = args.input.resolve()
    destination = args.output.resolve()
    if source == destination:
        raise SystemExit("refusing to overwrite the source executable")
    original_data = source.read_bytes()
    actual_hash = sha256(original_data)
    if actual_hash != EXPECTED_SHA256:
        raise SystemExit(f"wrong input executable SHA256: {actual_hash}")
    if len(PATCH) > len(ORIGINAL):
        raise SystemExit("patch body does not fit inside the original function")
    offset = target_file_offset(original_data)
    if original_data[offset:offset + len(ORIGINAL)] != ORIGINAL:
        raise SystemExit("target function bytes do not match the expected original")
    replacement = PATCH + b"\x90" * (len(ORIGINAL) - len(PATCH))
    output_data = original_data[:offset] + replacement + original_data[offset + len(ORIGINAL):]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(output_data)
    print(f"input SHA256:  {actual_hash}")
    print(f"output SHA256: {sha256(output_data)}")
    print(f"patched PE RVA: 0x{TARGET_RVA:x} (file offset 0x{offset:x})")
    print(f"modified bytes: {len(ORIGINAL)} (patch {len(PATCH)} + {len(ORIGINAL)-len(PATCH)} NOPs)")
    print(f"output: {destination}")


if __name__ == "__main__":
    main()
