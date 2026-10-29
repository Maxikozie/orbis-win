#!/usr/bin/env python3
# Pull prospero-win's ps5log session files off the console over GoldHEN FTP.
#   python3 -I scripts/pull-ps5log.py <out-dir>
# PW_WINE64 output goes HERE, not to klog. next.txt names the next index, so
# the live session is (next-1) % 8; the live file returns FTP 550 while the
# title holds it, and session-N.previous.log is the rotated copy.

from ftplib import FTP
import sys, os
outdir = sys.argv[1]
f = FTP(); f.connect('10.168.2.52', 2121, timeout=30); f.login()
buf = []
f.retrbinary('RETR /data/prospero-win/logs/next.txt', buf.append)
print('next.txt =', b''.join(buf).decode('ascii', 'replace').strip())
names = []
f.retrlines('NLST /data/prospero-win/logs', names.append)
for n in names:
    base = os.path.basename(n)
    if not base.startswith('session-'):
        continue
    dst = os.path.join(outdir, base)
    tmp = dst + '.part'
    try:
        with open(tmp, 'wb') as fh:
            f.retrbinary('RETR /data/prospero-win/logs/' + base, fh.write)
        os.replace(tmp, dst)          # only clobber a good copy on success
        print('%-18s %9d bytes' % (base, os.path.getsize(dst)))
    except Exception as e:
        try: os.unlink(tmp)
        except OSError: pass
        print('%-18s SKIPPED (%s)' % (base, str(e)[:40]))
f.quit()
