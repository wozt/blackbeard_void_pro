#!/usr/bin/env python3
"""Calculate a four-component release tag without modifying source files.

Examples: no tags -> v1.0.0.0, v1.0.0.0 -> v1.0.0.1.
The first three components are deliberately fixed until manually updated.
Only exact, canonical tags in this series contribute to the revision.
"""
from __future__ import annotations

import re
import subprocess
import sys
from collections.abc import Iterable

SERIES = "1.0.0"


def next_tag(tags: Iterable[str], series: str = SERIES) -> str:
    regex = re.compile(r"^v" + re.escape(series) + r"\.(0|[1-9][0-9]*)$")
    numbers = [int(m.group(1)) for tag in tags if (m := regex.fullmatch(tag))]
    return f"v{series}.{max(numbers, default=-1) + 1}"


def main() -> int:
    try:
        result = subprocess.run(
            ["git", "tag", "--list", "v" + SERIES + ".*"],
            check=True, text=True, capture_output=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"Unable to list Git tags: {exc}", file=sys.stderr)
        return 1
    print(next_tag(result.stdout.splitlines()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
