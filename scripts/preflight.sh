#!/usr/bin/env bash
# Everything that can be checked from this host before touching the console.
# Run it when the PS4 is on and idle at the home screen.
#
#   scripts/preflight.sh
#
# The FTP port is the one worth probing rather than assuming: GoldHEN's own
# server listens on 2121, but a stock FTP payload usually takes 1337 and some
# builds take 21. Guessing wrong looks like a hung upload, not a refused one.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
need_console

fail=0
note() { printf '  %-22s %s\n' "$1" "$2"; }

# --- reachable at all -------------------------------------------------------
if ping -c2 -W2 "$ORBIS_CONSOLE" >/dev/null 2>&1; then
  note "console" "$ORBIS_CONSOLE up"
else
  note "console" "$ORBIS_CONSOLE NOT RESPONDING"
  echo
  echo "  Power the PS4 on and leave it at the home screen. If it is on, check"
  echo "  that it took the address you expected — the DHCP lease may have moved."
  exit 1
fi

# Same broadcast domain means no router in the way for the UDP log path.
local_if=$(ip -4 route get "$ORBIS_CONSOLE" 2>/dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p')
local_ip=$(ip -4 route get "$ORBIS_CONSOLE" 2>/dev/null | sed -n 's/.* src \([^ ]*\).*/\1/p')
note "this host" "${local_ip:-?} via ${local_if:-?}"

# --- which port is actually serving FTP -------------------------------------
# A PS4 FTP server greets with a 220 line. Read it rather than trusting that
# the connect succeeded: something else on the LAN may hold the port.
probe() { timeout 3 bash -c "exec 3<>/dev/tcp/$1/$2 && head -c 96 <&3" 2>/dev/null; }

found=""
for port in "$ORBIS_FTP_PORT" 2121 1337 21; do
  [[ -z $port ]] && continue
  [[ ,$seen, == *",$port,"* ]] && continue
  seen="${seen:+$seen,}$port"
  banner=$(probe "$ORBIS_CONSOLE" "$port" | tr -d '\r' | head -1)
  if [[ $banner == 220* ]]; then
    note "ftp $port" "$banner"
    [[ -z $found ]] && found=$port
  else
    note "ftp $port" "no answer"
  fi
done

if [[ -z $found ]]; then
  note "ftp" "NOTHING LISTENING"
  echo
  echo "  No FTP server answered. On the console: GoldHEN → enable its FTP server,"
  echo "  or run an FTP payload. Nothing can be uploaded until one is up."
  fail=1
elif [[ $found != "$ORBIS_FTP_PORT" ]]; then
  echo
  echo "  !! config.env says ORBIS_FTP_PORT=$ORBIS_FTP_PORT but $found is the one answering."
  echo "     Fix it with:"
  echo "       sed -i 's/^ORBIS_FTP_PORT=.*/ORBIS_FTP_PORT=$found/' config.env"
  fail=1
fi

# --- klog, the fallback log channel -----------------------------------------
if timeout 3 bash -c "exec 3<>/dev/tcp/$ORBIS_CONSOLE/$PS4_KLOG_PORT" 2>/dev/null; then
  note "klog $PS4_KLOG_PORT" "open"
else
  note "klog $PS4_KLOG_PORT" "closed (optional — UDP netlog is the primary channel)"
fi

# --- the local UDP capture port has to be free ------------------------------
if ss -lun 2>/dev/null | grep -q ":$PS4_LOG_PORT "; then
  note "udp $PS4_LOG_PORT" "ALREADY BOUND — logs.sh will not start"
  fail=1
else
  note "udp $PS4_LOG_PORT" "free"
fi

# --- is there anything to send ----------------------------------------------
pkg="$ROOT/pkgout/triangle/GLDS10001.pkg"
if [[ -f $pkg ]]; then
  note "triangle pkg" "$(du -h "$(readlink -f "$pkg")" | cut -f1) ready"
else
  note "triangle pkg" "not built — run: ./scripts/build.sh triangle --pkg"
  fail=1
fi

echo
if (( fail )); then
  say "not ready — see above"
  exit 1
fi
say "ready. Start ./scripts/logs.sh in another terminal, then:"
echo "     ./scripts/build.sh triangle --deploy"
