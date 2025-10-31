#!/usr/bin/env python3
"""Run real external child cycles in one owned fixture script."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    parser.add_argument("--cycles", type=int, default=1000)
    args = parser.parse_args()
    if args.cycles < 1:
        raise SystemExit("cycles must be positive")
    script = ";\n".join("/usr/bin/true" for _ in range(args.cycles)) + "\n"
    started = time.monotonic_ns()
    result = subprocess.run([args.executable], input=script, text=True, capture_output=True, check=False)
    duration = time.monotonic_ns() - started
    evidence = {"schema": 1, "requested_cycles": args.cycles, "completed_cycles": args.cycles if result.returncode == 0 else None,
                "status": result.returncode, "duration_ns": duration, "stdout_bytes": len(result.stdout), "stderr_bytes": len(result.stderr),
                "transport": "stdin script pipe to avoid exec argument-size limits", "persistent_zombie_observation": "not performed by fast driver"}
    target = ROOT / ".agent-local" / ("stress-heavy.json" if args.cycles >= 180000 else "stress.json")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence))
    return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
