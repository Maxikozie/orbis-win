#!/usr/bin/env python3
"""Generate the export-name array used by the temporary libkernel probe."""

import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} LIBKERNEL_HEADER OUTPUT", file=sys.stderr)
        return 2

    header, output = map(Path, sys.argv[1:])
    names = []
    for line in header.read_text().splitlines():
        match = re.match(
            r"\s*(?:[A-Za-z_]\w*\s+|[A-Za-z_]\w*\s*\*\s*)+"
            r"([A-Za-z_]\w*)\s*\([^;]*\)\s*;",
            line,
        )
        if match and match.group(1) not in names:
            names.append(match.group(1))

    if not names:
        print(f"no libkernel declarations found in {header}", file=sys.stderr)
        return 1
    output.write_text("".join(f'    "{name}",\n' for name in names))
    print(f"generated {len(names)} candidate libkernel exports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
