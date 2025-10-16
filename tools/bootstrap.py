#!/usr/bin/env python3
"""Check reproducible local prerequisites without modifying user configuration."""

from __future__ import annotations

import shutil
import subprocess
import sys


REQUIRED = ("cmake", "ninja", "python3", "git")


def main() -> int:
    missing = [tool for tool in REQUIRED if shutil.which(tool) is None]
    if missing:
        print("missing prerequisites: " + ", ".join(missing), file=sys.stderr)
        return 2
    print("Splice bootstrap prerequisites:")
    for tool in REQUIRED:
        path = shutil.which(tool)
        version = subprocess.run([tool, "--version"], text=True, capture_output=True, check=False).stdout.splitlines()
        print(f"  {tool}: {path} {version[0] if version else ''}")
    print("No downloaded toolchain or user dotfile changes are required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
