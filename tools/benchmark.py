#!/usr/bin/env python3
"""Measure the frozen external-dispatch corpus without claiming target success."""

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


def sample(command: list[str], repetitions: int, warmup: int) -> list[float]:
    for _ in range(warmup):
        subprocess.run(command, env={**os.environ, "LC_ALL": "C"}, stdin=subprocess.DEVNULL,
                       capture_output=True, text=True, check=False)
    values = []
    for _ in range(repetitions):
        start = time.monotonic_ns()
        result = subprocess.run(command, env={**os.environ, "LC_ALL": "C"}, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, check=False)
        elapsed = (time.monotonic_ns() - start) / 1_000_000
        if result.returncode != 0 or not result.stdout.endswith("PROBE"):
            raise RuntimeError(f"late or missing launch marker: {command} status={result.returncode} stdout={result.stdout!r}")
        values.append(elapsed)
    return values


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    position = min(len(ordered) - 1, int(round((len(ordered) - 1) * fraction)))
    return ordered[position]


def summarize(shell: str, values: list[float]) -> dict:
    return {"shell": shell, "median_ms": statistics.median(values), "p95_ms": percentile(values, .95),
            "p99_ms": percentile(values, .99), "mean_ms": statistics.mean(values), "sample_count": len(values)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    args = parser.parse_args()
    protocol = json.loads((ROOT / "benchmarks" / "protocol.json").read_text())
    workloads = []
    for workload in protocol["corpus"]:
        script = workload["script"] + "; /usr/bin/printf PROBE"
        splice_values = sample([args.executable, "-c", script], args.repetitions, args.warmup)
        row = {"id": workload["id"], "script": workload["script"], "splice": summarize("splice", splice_values),
               "raw_splice_ms": splice_values}
        if shutil.which("bash"):
            bash_values = sample(["bash", "--noprofile", "--norc", "-c", script], args.repetitions, args.warmup)
            row["bash"] = summarize("bash", bash_values)
            row["raw_bash_ms"] = bash_values
        workloads.append(row)
    splice_medians = [row["splice"]["median_ms"] for row in workloads]
    bash_medians = [row["bash"]["median_ms"] for row in workloads if "bash" in row]
    reduction = None
    if bash_medians:
        baseline = statistics.median(bash_medians)
        measured = statistics.median(splice_medians)
        reduction = 100 * (baseline - measured) / baseline if baseline else None
    evidence = {"schema": 1, "protocol": str(ROOT / "benchmarks/protocol.json"), "repetitions": args.repetitions,
                "warmup": args.warmup, "endpoint": "subprocess start to final PROBE stdout marker",
                "workload_count": len(workloads), "workloads": workloads,
                "dispatch_reduction_percent": reduction, "target_status": "computed-not-accepted"}
    target = ROOT / ".agent-local" / "benchmark.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"workload_count": len(workloads), "repetitions": args.repetitions,
                      "dispatch_reduction_percent": reduction, "target_status": "computed-not-accepted"}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
