#!/usr/bin/env python3
"""Observe genuinely live child process groups without sweeping by executable name."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def live_groups(shell_pid: int) -> set[int]:
    result = subprocess.run(["ps", "-axo", "pid=,ppid=,pgid=,stat="], text=True, capture_output=True, check=False)
    groups = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        try:
            pid, parent, group = (int(fields[index]) for index in range(3))
        except ValueError:
            continue
        if parent == shell_pid and pid != shell_pid and "Z" not in fields[3]:
            groups.add(group)
    return groups


def one(executable: str, groups: int, hold_seconds: float) -> dict:
    script = "".join("/bin/sleep %.3f &\n" % hold_seconds for _ in range(groups)) + "/bin/sleep %.3f\n" % hold_seconds
    process = subprocess.Popen([executable, "-c", script], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peak = 0
    deadline = time.monotonic() + max(hold_seconds * 2, 2.0)
    while process.poll() is None and time.monotonic() < deadline:
        peak = max(peak, len(live_groups(process.pid)))
        time.sleep(0.005)
    stdout, stderr = process.communicate(timeout=10)
    return {"shell_pid": process.pid, "requested_groups": groups, "peak_live_groups": peak,
            "status": process.returncode, "stdout_bytes": len(stdout), "stderr": stderr[-400:]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    parser.add_argument("--groups", type=int, default=500)
    parser.add_argument("--repetitions", type=int, default=1)
    parser.add_argument("--hold-seconds", type=float, default=0.25)
    args = parser.parse_args()
    rows = [one(args.executable, args.groups, args.hold_seconds) for _ in range(args.repetitions)]
    evidence = {"schema": 1, "requested_groups": args.groups, "repetitions": args.repetitions, "hold_seconds": args.hold_seconds, "runs": rows,
                "minimum_peak": min((row["peak_live_groups"] for row in rows), default=0),
                "observation": "ps records were filtered by exact shell parent PID and zombie state"}
    target = ROOT / ".agent-local" / "concurrency.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence))
    return 0 if all(row["status"] == 0 for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
