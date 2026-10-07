#!/usr/bin/env python3
"""Minimal FTP upload for GoldHEN's server. Nothing optional is sent.

  scripts/ftp-put.py <host> <port> <local-file> <remote-path> [user] [pass]

With no user given, NO LOGIN IS ATTEMPTED at all: it goes straight to TYPE I.
GoldHEN's FTP server requires no authentication, and an anonymous login is
what killed it twice (reported by the console's owner, who watched it happen:
"before you logged in it worked, then once you logged in it died").

⚠ WHY THIS EXISTS INSTEAD OF lftp.

Observed 2026-10-06 on a PS4 Pro running GoldHEN FTP v2.2: the server answers
its banner happily, and dies the moment lftp logs in — taking the whole GoldHEN
payload with it, klog included, leaving a console that still answers ping. Two
runs, same shape both times, and the console had to be rebooted each time.

lftp is a full-featured client. On connect it sends FEAT, OPTS UTF8, SYST, PWD
and prefers EPSV over PASV, any of which a small embedded server may not handle.
This sends only what STOR actually requires:

    USER / PASS (only if challenged) / TYPE I / PASV / STOR

and nothing else. No FEAT, no SYST, no OPTS, no EPSV, no PWD, no SIZE, no MLSD.

⚠ EVERY COMMAND AND REPLY IS PRINTED BEFORE IT IS SENT. If the server dies again
the log names the exact command that did it, which is the whole point — the
previous two attempts cost a reboot each and told us only "connection refused".
"""
import socket
import sys


class Died(Exception):
    pass


def main() -> int:
    if len(sys.argv) not in (5, 6, 7):
        print(__doc__)
        return 2
    host, port, local, remote = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    user = sys.argv[5] if len(sys.argv) > 5 else ""
    password = sys.argv[6] if len(sys.argv) > 6 else ""

    with open(local, "rb") as fh:
        payload = fh.read()
    print(f"   local  {local}  {len(payload)} B")
    print(f"   remote {remote}")

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
        parent = remote.rsplit("/", 1)[0]
        if parent:
            send(f"MKD {parent}")

        r = send("PASV")
        if not r.startswith("227"):
            raise Died(f"PASV refused: {r}")
        nums = r[r.index("(") + 1:r.index(")")].split(",")
        dhost = ".".join(nums[:4])
        dport = int(nums[4]) * 256 + int(nums[5])
        print(f"   data   {dhost}:{dport}")

        data = socket.create_connection((dhost, dport), timeout=15)

        r = send(f"STOR {remote}")
        if not r.startswith(("150", "125")):
            data.close()
            raise Died(f"STOR refused: {r}")

        sent = 0
        step = max(len(payload) // 20, 1 << 20)
        nxt = step
        while sent < len(payload):
            n = data.send(payload[sent:sent + 65536])
            if n == 0:
                raise Died("data connection closed early")
            sent += n
            if sent >= nxt:
                print(f"   ... {sent * 100 // len(payload)}%")
                nxt += step
        data.close()
        print(f"   sent   {sent} B")

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
