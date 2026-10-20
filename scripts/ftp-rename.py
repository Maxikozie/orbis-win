#!/usr/bin/env python3
"""Rename one known GoldHEN FTP path without login, probes, or listings.

Usage: ftp-rename.py HOST PORT OLD-PATH NEW-PATH
"""
import socket
import sys


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    host, port, old, new = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    if not old.startswith("/") or not new.startswith("/") or any(c in old + new for c in "\r\n"):
        raise ValueError("paths must be absolute and contain no line breaks")
    control = socket.create_connection((host, port), timeout=15)
    reader = control.makefile("rb")

    def reply(expected: tuple[bytes, ...] = ()) -> bytes:
        line = reader.readline()
        if not line:
            raise RuntimeError("FTP control connection closed")
        line = line.rstrip(b"\r\n")
        print(f"<-- {line.decode('utf-8', 'replace')}", flush=True)
        if expected and not line.startswith(expected):
            raise RuntimeError(f"unexpected FTP reply: {line.decode('utf-8', 'replace')}")
        return line

    def command(value: str, expected: tuple[bytes, ...]) -> None:
        print(f"--> {value}", flush=True)
        control.sendall(value.encode() + b"\r\n")
        reply(expected)

    try:
        reply((b"220",))
        command(f"RNFR {old}", (b"350",))
        command(f"RNTO {new}", (b"250", b"226"))
        command("QUIT", (b"221",))
    finally:
        control.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FTP rename failed: {error}", file=sys.stderr)
        raise SystemExit(1)
