#!/usr/bin/env bash
# What this host needs to cross-build for the PS4, and whether it has it.
# Checks only. Prints the install command rather than running it.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# clang/clang++/llvm-ar/llvm-ranlib/ld.lld: the SDK ships no compiler. libc and
# libc++ are prebuilt in the bundle; the host supplies the toolchain that uses them.
# glslangValidator: the triangle compiles SPIR-V on the host and embeds it, so the
# console reads no shader files. lftp: deploy.sh's transport.
TOOLS=(clang clang++ llvm-ar llvm-ranlib ld.lld cmake ninja glslangValidator lftp)
APT=(clang lld llvm cmake ninja-build glslang-tools lftp)

missing=0
for t in "${TOOLS[@]}"; do
  if p=$(command -v "$t" 2>/dev/null); then
    printf '  %-18s %s\n' "$t" "$p"
  else
    printf '  %-18s MISSING\n' "$t"; missing=$((missing + 1))
  fi
done

echo
if (( missing )); then
  echo "$missing missing. On Debian/Ubuntu:"
  echo
  echo "    sudo apt-get install -y ${APT[*]}"
  exit 1
fi

# The bundle was cut against LLVM 18. Clang 19 builds it clean (verified
# 2026-10-06 on Debian 13 — hello, triangle and a packaged .pkg). Newer is
# untested, so say so rather than failing on it.
ver=$(clang --version | sed -n '1s/.*version \([0-9]*\).*/\1/p')
[[ -n $ver && $ver -ge 20 ]] &&
  echo "note: clang $ver — the bundle targets LLVM 18 and is verified on 19. Untested here."

say "host is ready"
