#!/usr/bin/env python3
"""Minimal FTP download for GoldHEN's server. Nothing optional is sent.

  scripts/ftp-get.py <host> <port> <remote-path> <local-file> [user] [pass]

The sibling of ftp-put.py, and under the same rule: with no user given, NO
LOGIN IS ATTEMPTED. GoldHEN's FTP server needs none, and an anonymous login is
what killed it twice -- see ftp-put.py, which carries the full account.

Sends only what RETR requires:

    USER / PASS (only if challenged) / TYPE I / PASV / RETR

⚠ WHY THIS EXISTS. A PRX other than ntdll has no usable output: its stderr goes
to a sink ntdll owns, and sceKernelDebugOutText from wowprospero.prx has never
produced a line in klog, though the symbol is imported exactly as wineserver's
is. A file on /data is independent of both, survives the exit, and holds more
than a fault storm would fit through klog anyway.
"""

import socket
import sys


class Died(Exception):
    pass


def main() -> int:
    if len(sys.argv) not in (5, 6, 7):
        print(__doc__)
        return 2
    host, port, remote, local = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    user = sys.argv[5] if len(sys.argv) > 5 else ""
    password = sys.argv[6] if len(sys.argv) > 6 else ""

    print(f"   remote {remote}")
    print(f"   local  {local}")

    ctl = socket.create_connection((host, port), timeout=15)
    f = ctl.makefile("rb")

    def reply() -> str:
        line = f.readline()
        if not line:
            raise Died("server closed the control connection")
        text = line.decode("utf-8", "replace").rstrip()
        print(f"   <-- {text}")
        # A multi-line reply is "250-first" ... "250 last".
        if len(text) > 3 and text[3] == "-":
            code = text[:3]
            while True:
                more = f.readline()
                if not more:
                    raise Died("server closed mid multi-line reply")
                mtext = more.decode("utf-8", "replace").rstrip()
                print(f"   <-- {mtext}")
                if mtext.startswith(code) and len(mtext) > 3 and mtext[3] == " ":
                    return mtext
        return text

    def send(cmd: str, hide: bool = False) -> str:
        print(f"   --> {'PASS ****' if hide else cmd}")
        ctl.sendall(cmd.encode() + b"\r\n")
        return reply()

    try:
        reply()                                    # the 220 greeting

        # ⚠ NO LOGIN BY DEFAULT. This server needs none, and `USER anonymous`
        # took the whole GoldHEN payload down twice. Only send credentials if
        # somebody explicitly asks for them.
        if user:
            r = send(f"USER {user}")
            if r.startswith("331"):                # only if actually challenged
                r = send(f"PASS {password}", hide=True)
            if not r.startswith(("230", "202")):
                raise Died(f"login refused: {r}")
        else:
            print("   (no login — going straight to TYPE I)")

        if not send("TYPE I").startswith("200"):
            raise Died("TYPE I refused")

        # ⚠ /data/pkg DOES NOT EXIST ON A FRESH CONSOLE. The bundle's deploy.sh
        # says "/data/pkg and nowhere else" and assumes it is there; on this
        # console it was not, and STOR answered 550 File unavailable. /data
        # itself is drwxrwxrwx, so create the parent and carry on. An existing
        # directory answers 550 here too, which is why the reply is ignored
        # rather than checked.
        r = send("PASV")
        if not r.startswith("227"):
            raise Died(f"PASV refused: {r}")
        nums = r[r.index("(") + 1:r.index(")")].split(",")
        dhost = ".".join(nums[:4])
        dport = int(nums[4]) * 256 + int(nums[5])
        print(f"   data   {dhost}:{dport}")

        data = socket.create_connection((dhost, dport), timeout=15)

        r = send(f"RETR {remote}")
        if not r.startswith(("150", "125")):
            data.close()
            raise Died(f"RETR refused: {r}")

        chunks, got = [], 0
        while True:
            chunk = data.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            got += len(chunk)
        data.close()
        with open(local, "wb") as fh:
            fh.write(b"".join(chunks))
        print(f"   got    {got} B")

        if not reply().startswith("226"):
            raise Died("transfer not confirmed")

        send("QUIT")
    except Died as e:
        print(f"!! {e}", file=sys.stderr)
        return 1
    except (OSError, socket.timeout) as e:
        print(f"!! socket: {e}", file=sys.stderr)
        return 1
    finally:
        try:
            ctl.close()
        except OSError:
            pass

    print("   ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
