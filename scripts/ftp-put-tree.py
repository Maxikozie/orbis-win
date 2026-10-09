#!/usr/bin/env python3
"""Upload a prepared directory tree over one GoldHEN FTP connection.

Usage: ftp-put-tree.py HOST PORT LOCAL-DIR REMOTE-DIR
No login, service probes, listing, deletion, or symlink traversal. Uses only
MKD, TYPE I, PASV, STOR and QUIT, like the project's single-file uploader.
"""
import ftplib
from pathlib import Path, PurePosixPath
import sys


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    host, port, local, remote = sys.argv[1:]
    root = Path(local).resolve()
    if not root.is_dir() or not remote.startswith("/"):
        raise ValueError("expected a local directory and an absolute remote path")
    paths = sorted(root.rglob("*"))
    if any(p.is_symlink() for p in paths):
        raise ValueError("materialize symlinks before uploading")
    if any(c in remote for c in "\r\n") or any("\r" in str(p) or "\n" in str(p) for p in paths):
        raise ValueError("newline in FTP path")
    if ".." in PurePosixPath(remote).parts:
        raise ValueError("parent traversal in remote path")
    files = [p for p in paths if p.is_file()]
    directories = {remote.rstrip("/")}
    directories.update(remote.rstrip("/") + "/" + p.relative_to(root).as_posix()
                       for p in paths if p.is_dir())
    parts = PurePosixPath(remote).parts
    directories.update(str(PurePosixPath(*parts[:i])) for i in range(2, len(parts)))
    print(f"Uploading {len(files)} files to {host}:{port}{remote}; no login", flush=True)
    ftp = ftplib.FTP(timeout=30)
    try:
        print(ftp.connect(host, int(port)), flush=True)
        for directory in sorted(directories, key=lambda p: (p.count("/"), p)):
            try:
                ftp.mkd(directory)
            except ftplib.error_perm as error:
                # GoldHEN returns 550 for a directory that already exists.
                # An inaccessible directory will fail at its first STOR.
                if not str(error).startswith("550"):
                    raise
        total = 0
        for index, path in enumerate(files, 1):
            target = remote.rstrip("/") + "/" + path.relative_to(root).as_posix()
            with path.open("rb") as stream:
                reply = ftp.storbinary("STOR " + target, stream, blocksize=65536)
            if not reply.startswith("226"):
                raise RuntimeError(f"transfer unconfirmed: {target}: {reply}")
            total += path.stat().st_size
            if index % 50 == 0 or index == len(files):
                print(f"{index}/{len(files)} files, {total // 1024} KiB confirmed", flush=True)
        print(ftp.quit(), flush=True)
    finally:
        ftp.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ftplib.Error, ValueError, RuntimeError) as error:
        print(f"FTP upload failed: {error}", file=sys.stderr)
        sys.exit(1)
