#!/usr/bin/env bash
# Download, checksum and unpack the orbis-sdk-v1 bundle into sdk/.
#
# The bundle pins the OpenOrbis SDK, the orbis-compat overlay and Mesa/RADV
# together on purpose: a Mesa built against one header tree and linked against
# another is a struct-layout bug that compiles cleanly and fails at run time.
# Take the bundle; do not assemble the three yourself.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

REPO=orbis-ports/orbis-porting-kit
TAG=orbis-sdk-v1
DEST="$ROOT/sdk"

[[ -f "$BUNDLE/BUNDLE.txt" ]] && { say "already unpacked at $BUNDLE"; exit 0; }

mkdir -p "$DEST"
cd "$DEST"

if [[ ! -f $TAG.tar.gz ]]; then
  say "downloading $TAG (~92 MB)"
  command -v gh >/dev/null &&
    gh release download "$TAG" --repo "$REPO" --clobber ||
    { base="https://github.com/$REPO/releases/download/$TAG"
      curl -fL# -o "$TAG.tar.gz"        "$base/$TAG.tar.gz"
      curl -fL# -o "$TAG.tar.gz.sha256" "$base/$TAG.tar.gz.sha256"; }
fi

say "checksum"
sha256sum -c "$TAG.tar.gz.sha256" || die "checksum mismatch — delete sdk/ and retry"

say "unpacking"
tar -xzf "$TAG.tar.gz"

# verify.sh is offline and checks the one thing that matters most: that this
# bundle's Mesa and this bundle's orbis-compat were built from the same pair.
say "verify.sh"
"$BUNDLE/verify.sh" "$BUNDLE" || die "verify.sh failed"

grep -E '^(mesa-version|openorbis-sdk-tag|pairing|gate)=' "$BUNDLE/BUNDLE.txt"
say "ready"
