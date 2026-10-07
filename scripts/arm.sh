#!/usr/bin/env bash
# Wait for GoldHEN, make sure the console holds the packages we think it does,
# then start capturing. For running across a reboot/re-jailbreak.
#
#   scripts/arm.sh
#
# ⚠ IT RE-UPLOADS RATHER THAN ASSUMING. /data survives a reboot, so the packages
# usually are still there — but "usually" is how a stale build gets launched and
# measured. Sizes are compared against local and anything that differs is sent
# again.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_console

banner() {
  timeout 4 bash -c '
    exec 3<>/dev/tcp/'"$ORBIS_CONSOLE"'/'"$ORBIS_FTP_PORT"' 2>/dev/null || exit 1
    IFS= read -r -t 3 l <&3 || exit 1
    printf "%s" "$l"' 2>/dev/null | tr -d '\r' || true
}

say "waiting for GoldHEN on $ORBIS_CONSOLE:$ORBIS_FTP_PORT"
until [[ $(banner) == 220* ]]; do sleep 5; done
say "up: $(banner)"

for t in present probe triangle; do
  local_pkg=$(readlink -f "$ROOT/pkgout/$t"/*.pkg 2>/dev/null | head -1)
  [[ -f $local_pkg ]] || { say "no local $t package, skipping"; continue; }
  want=$(stat -c%s "$local_pkg")
  have=$(python3 "$ROOT/scripts/ftp-size.py" "$ORBIS_CONSOLE" "$ORBIS_FTP_PORT" "/data/pkg/$t.pkg" 2>/dev/null || echo 0)
  if [[ "$have" == "$want" ]]; then
    say "$t already on console ($have B)"
  else
    say "$t differs (console=$have want=$want) - uploading"
    "$ROOT/scripts/deploy.sh" "$t" 2>&1 | grep -E "sent |^   ok|^!!"
  fi
done

# ⚠ The env file must NOT be present: the patched driver chooses 4 engines from
# sceKernelIsNeoMode(), and a leftover ORBIS_NUM_SE would make the result
# unattributable between the two.
python3 - "$ORBIS_CONSOLE" "$ORBIS_FTP_PORT" <<'PY' || true
import socket, sys
ctl = socket.create_connection((sys.argv[1], int(sys.argv[2])), timeout=10); f = ctl.makefile("rb")
f.readline()
ctl.sendall(b"DELE /data/orbis-env.txt\r\n")
r = f.readline().decode("utf-8","replace").strip()
print("== orbis-env.txt:", "removed" if r.startswith("250") else "not present")
ctl.sendall(b"QUIT\r\n"); ctl.close()
PY

say "starting klog capture"
exec "$ROOT/scripts/logs.sh" klog
