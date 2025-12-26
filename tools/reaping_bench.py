#!/usr/bin/env python3
"""Measure child process reaping latency between child exit and parent waitpid."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import statistics
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def measure_reaping_latency(shell_cmd: list[str], repetitions: int, count: int) -> list[float]:
    """Measure the time taken to spawn and reap a batch of quick-exiting children."""
    samples = []
    # Workload: spawn N background children and wait for all of them
    script = " ".join(f"/usr/bin/true &" for _ in range(count)) + " wait"
    cmd = shell_cmd + [script]

    # Warmup
    for _ in range(2):
        subprocess.run(cmd, env={**os.environ, "LC_ALL": "C"}, capture_output=True, check=False)

    for _ in range(repetitions):
        start = time.monotonic_ns()
        result = subprocess.run(cmd, env={**os.environ, "LC_ALL": "C"}, capture_output=True, check=False)
        elapsed_ms = (time.monotonic_ns() - start) / 1_000_000.0
        if result.returncode != 0:
            raise RuntimeError(f"reaping benchmark command failed: {result.stderr}")
        samples.append(elapsed_ms)
    return samples


def summarize(name: str, samples: list[float]) -> dict:
    ordered = sorted(samples)
    p95_idx = min(len(ordered) - 1, int(round((len(ordered) - 1) * 0.95)))
    p99_idx = min(len(ordered) - 1, int(round((len(ordered) - 1) * 0.99)))
    return {
        "name": name,
        "sample_count": len(samples),
        "mean_ms": statistics.mean(samples),
        "median_ms": statistics.median(samples),
        "p95_ms": ordered[p95_idx],
        "p99_ms": ordered[p99_idx],
        "min_ms": ordered[0],
        "max_ms": ordered[-1],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark child process reaping latency")
    parser.add_argument("--executable", default=str(ROOT / "build-release" / "splice"))
    parser.add_argument("--children", type=int, default=20, help="Number of background children per cycle")
    parser.add_argument("--repetitions", type=int, default=15, help="Number of benchmark iterations")
    parser.add_argument("--output", default=str(ROOT / ".agent-local" / "reaping.json"))
    args = parser.parse_args()

    splice_path = str(pathlib.Path(args.executable).resolve())
    splice_samples = measure_reaping_latency([splice_path, "-c"], args.repetitions, args.children)
    splice_summary = summarize("splice", splice_samples)

    bash_summary = None
    reduction_percent = None
    if shutil.which("bash"):
        bash_samples = measure_reaping_latency(["bash", "--noprofile", "--norc", "-c"],
                                               args.repetitions, args.children)
        bash_summary = summarize("bash", bash_samples)
        if bash_summary["median_ms"] > 0:
            reduction_percent = 100.0 * (bash_summary["median_ms"] - splice_summary["median_ms"]) / bash_summary["median_ms"]

    result = {
        "schema": 1,
        "proxy": "child completion observed by waitpid; final-write timestamp is not zombie creation time",
        "target_reduction_percent": 45.0,
        "children_per_repetition": args.children,
        "repetitions": args.repetitions,
        "splice": splice_summary,
        "bash": bash_summary,
        "reaping_reduction_percent": reduction_percent,
        "raw_splice_ms": splice_samples,
    }

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({
        "children": args.children,
        "splice_median_ms": splice_summary["median_ms"],
        "bash_median_ms": bash_summary["median_ms"] if bash_summary else None,
        "reaping_reduction_percent": reduction_percent,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
