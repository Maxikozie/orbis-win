#!/usr/bin/env bash
# Stage 2: pull a radeon_info dump out of a capture log and diff it against the
# Liverpool baseline from the PS4 RADV port's own record.
#
#   scripts/logs.sh                      # in one terminal, then run the title
#   scripts/capture-radeon-info.sh       # here, afterwards
#
# RADV_DEBUG=info makes the driver print radeon_info. ⚠ That dump does NOT go
# through mesa_log, and on this platform it cannot go through stdout either —
# freopen fails. The project's own harness patches the one call site to fopen a
# file directly; without that the file comes back empty and reads as if the
# driver never ran. The UDP relay prefixes its lines with "radv-info|".
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

OUT="$ROOT/notes/radeon_info-gladius.txt"
BASE="$ROOT/notes/radeon_info-liverpool.txt"
mkdir -p "$ROOT/notes"

latest=$(ls -t "$PS4_LOG_DIR"/ps4-*.log 2>/dev/null | head -1) ||
  die "no capture logs in $PS4_LOG_DIR"
[[ -n $latest ]] || die "no capture logs in $PS4_LOG_DIR"
say "reading $latest"

sed -n 's/^[0-9:.]* radv-info| //p' "$latest" > "$OUT"
[[ -s $OUT ]] || die "no radv-info lines in $latest — was RADV_DEBUG=info set?"
say "wrote $OUT ($(wc -l < "$OUT") fields)"

if [[ ! -f $BASE ]]; then
  say "fetching the Liverpool baseline"
  curl -fsSL -o "$BASE" \
    https://raw.githubusercontent.com/orbis-ports/ps4-mesa-docs/main/notes/radeon_info-console.txt ||
    die "could not fetch the baseline"
fi

echo
say "Gladius vs Liverpool — every differing field"
diff "$BASE" "$OUT" || true
echo
# The fields that decide the ring-sizing work in stage 3.
say "the ones that matter"
grep -E '^\s*(marketing_name|name|num_cu|max_se|num_cu_per_sh|family|family_id|gfx_level|vram_size) ' "$OUT" || true
