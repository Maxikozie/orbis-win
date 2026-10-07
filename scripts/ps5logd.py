#!/usr/bin/env python3
"""Minimal ps5logd: the listener ps5log ships its lines to.

The title's ps5log reads /app0/dev.conf, /data/homebrew/dev.conf or ./dev.conf
for a destination and streams log lines there. Without a listener the connect
fails and -- because ps5log must never block the caller -- the title simply
runs on in silence, which is exactly how the first run looked.

TCP by default (DEV_UDP=0), one line per message, printed and appended to
build-ps4-logs/ps5log-<date>.log.
"""
import datetime as dt, pathlib, socket, sys, threading

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9300
OUT = pathlib.Path(__file__).resolve().parent.parent / "build-ps4-logs"
OUT.mkdir(parents=True, exist_ok=True)
path = OUT / f"ps5log-{dt.datetime.now():%Y%m%d-%H%M%S}.log"

def emit(line):
    stamp = f"{dt.datetime.now():%H:%M:%S.%f}"[:12]
    text = f"{stamp} {line}"
    print(text, flush=True)
    with path.open("a") as f:
        f.write(text + "\n")

def serve(conn, addr):
    emit(f"--- connected from {addr[0]}:{addr[1]} ---")
    buf = b""
    with conn:
        while True:
            try:
                chunk = conn.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                emit(line.decode("utf-8", "replace").rstrip("\r"))
    if buf:
        emit(buf.decode("utf-8", "replace"))
    emit(f"--- disconnected {addr[0]} ---")

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(8)
emit(f"ps5logd listening on 0.0.0.0:{PORT} (tcp) -> {path}")
while True:
    conn, addr = srv.accept()
    threading.Thread(target=serve, args=(conn, addr), daemon=True).start()
