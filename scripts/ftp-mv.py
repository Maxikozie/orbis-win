#!/usr/bin/env python3
"""Rename a path on GoldHEN's FTP server, with ftp-put.py's discipline.

  scripts/ftp-mv.py <host> <port> <from> <to>

NO LOGIN IS ATTEMPTED, for the reason ftp-put.py documents at length: GoldHEN's
server needs none, and logging in has killed it twice. Only RNFR and RNTO are
sent, and every command and reply is printed before it is sent.

⚠ WHY RENAME RATHER THAN DELETE. A directory can be moved aside without being
read. The prefix's .wineserver is drwx------ owned by the title, so this FTP
server (a different uid) cannot list it, and entering it crashes the console's
own file manager -- so neither emptying it nor recursing into it is available.
Renaming needs neither: Wine makes a fresh one on the next run, and the old
directory stays on disk to be looked at rather than being destroyed.
"""
import socket, sys


class Died(Exception):
    pass


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    host, port, src, dst = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]

    s = socket.create_connection((host, port), timeout=30)
    f = s.makefile("rwb")

    def reply() -> str:
        line = f.readline().decode("utf-8", "replace").rstrip()
        print(f"   <-- {line}")
        if not line:
            raise Died("server closed the connection")
        return line

    def send(cmd: str) -> str:
        print(f"   --> {cmd}")
        f.write((cmd + "\r\n").encode())
        f.flush()
        return reply()

    print(f"== renaming on {host}:{port}")
    print(f"   from {src}")
    print(f"   to   {dst}")
    reply()                      # greeting
    print("   (no login -- see the module docstring)")
    r = send(f"RNFR {src}")
    if not r.startswith("350"):
        print("!! RNFR refused; nothing was changed", file=sys.stderr)
        return 1
    r = send(f"RNTO {dst}")
    if r[:1] != "2":
        print("!! RNTO refused; nothing was changed", file=sys.stderr)
        return 1
    send("QUIT")
    print("   ok")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Died as e:
        print(f"!! {e}", file=sys.stderr)
        sys.exit(1)
