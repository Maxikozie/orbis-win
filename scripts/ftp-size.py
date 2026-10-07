#!/usr/bin/env python3
"""Size of one remote file, via LIST. Prints 0 if absent."""
import socket, sys, posixpath
host, port, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
try:
    ctl = socket.create_connection((host, port), timeout=10); f = ctl.makefile("rb")
    f.readline()
    def snd(c):
        ctl.sendall(c.encode() + b"\r\n")
        return f.readline().decode("utf-8", "replace").strip()
    snd("TYPE A")
    r = snd("PASV")
    n = r[r.index("(") + 1:r.index(")")].split(",")
    d = socket.create_connection((".".join(n[:4]), int(n[4]) * 256 + int(n[5])), timeout=10)
    r = snd(f"LIST {posixpath.dirname(path)}")
    buf = b""
    if r.startswith(("150", "125")):
        while True:
            c = d.recv(4096)
            if not c: break
            buf += c
        f.readline()
    d.close(); snd("QUIT"); ctl.close()
    want = posixpath.basename(path)
    for line in buf.decode("utf-8", "replace").splitlines():
        p = line.split()
        if p and p[-1] == want:
            print(p[4]); sys.exit(0)
except Exception:
    pass
print(0)
