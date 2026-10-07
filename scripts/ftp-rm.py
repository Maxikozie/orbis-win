#!/usr/bin/env python3
"""List or delete files on GoldHEN's FTP server, with ftp-put.py's discipline.

  scripts/ftp-rm.py <host> <port> list <remote-dir>
  scripts/ftp-rm.py <host> <port> rm   <remote-path> [<remote-path> ...]

NO LOGIN IS ATTEMPTED, for the reason ftp-put.py documents at length: GoldHEN's
server needs none, and logging in has killed it. Only the commands the task
requires are sent -- TYPE I, PASV, NLST for a listing; DELE for a removal --
and every command and reply is printed before it is sent, so if the server dies
the log names what did it.

`list` first, `rm` second: never delete a path that has not been seen.
"""
import socket, sys


class Died(Exception):
    pass


def main() -> int:
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    host, port, action = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    targets = sys.argv[4:]

    ctl = socket.create_connection((host, port), timeout=15)
    f = ctl.makefile("rb")

    def reply() -> str:
        line = f.readline()
        if not line:
            raise Died("server closed the control connection")
        text = line.decode("utf-8", "replace").rstrip()
        print(f"   <-- {text}")
        if len(text) > 3 and text[3] == "-":
            while True:
                more = f.readline()
                if not more:
                    raise Died("server closed during a multi-line reply")
                m = more.decode("utf-8", "replace").rstrip()
                print(f"   <-- {m}")
                if len(m) > 3 and m[3] == " " and m[:3].isdigit():
                    return m
        return text

    def send(cmd: str) -> str:
        print(f"   --> {cmd}")
        ctl.sendall(cmd.encode() + b"\r\n")
        return reply()

    def pasv() -> socket.socket:
        r = send("PASV")
        nums = r[r.index("(") + 1:r.index(")")].split(",")
        return socket.create_connection(
            (".".join(nums[:4]), int(nums[4]) * 256 + int(nums[5])), timeout=15)

    reply()                       # banner
    send("TYPE I")
    rc = 0
    if action == "list":
        data = pasv()
        send(f"NLST {targets[0]}")
        body = b""
        while True:
            chunk = data.recv(4096)
            if not chunk:
                break
            body += chunk
        data.close()
        reply()                   # transfer complete
        for name in body.decode("utf-8", "replace").split():
            print(f"   >>> {name}")
    elif action == "rm":
        for path in targets:
            r = send(f"DELE {path}")
            if not r.startswith("250"):
                print(f"   !!! not deleted: {path}")
                rc = 1
    else:
        print(__doc__)
        rc = 2
    send("QUIT")
    ctl.close()
    return rc


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Died as exc:
        print(f"   !!! {exc}")
        sys.exit(1)
