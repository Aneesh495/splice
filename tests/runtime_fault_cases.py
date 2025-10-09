#!/usr/bin/env python3
"""Small deterministic launch-failure checks that do not sweep processes."""

from __future__ import annotations

import pathlib
import subprocess
import sys


EXECUTABLE = pathlib.Path(sys.argv[1])


def run(command: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(EXECUTABLE), "-c", command], text=True, capture_output=True, check=False)


def main() -> int:
    missing = run("splice-command-that-does-not-exist")
    assert missing.returncode == 127, (missing.returncode, missing.stderr)
    assert "child launch failed" in missing.stderr, missing.stderr

    permission = run("/tmp")
    assert permission.returncode in (126, 127), permission.returncode

    invalid = subprocess.run([str(EXECUTABLE), "-n", "-c", "echo hi >"], text=True,
                             capture_output=True, check=False)
    assert invalid.returncode == 2 and "redirection" in invalid.stderr, invalid.stderr
    print("runtime fault cases: 3 passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
