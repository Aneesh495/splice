#!/usr/bin/env python3
"""Run the bounded local acceptance campaign and derive gate values from evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "acceptance" / "evidence"


def command_result(name: str, command: list[str]) -> dict:
    started = time.monotonic_ns()
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
    return {"name": name, "command": command, "status": result.returncode, "duration_ns": time.monotonic_ns() - started,
            "stdout": result.stdout, "stderr": result.stderr}


def copy_json(source: pathlib.Path, name: str) -> pathlib.Path | None:
    if not source.exists():
        return None
    target = EVIDENCE / name
    shutil.copyfile(source, target)
    return target


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (ROOT / ".agent-local").mkdir(exist_ok=True)
    steps = []
    steps.append(command_result("ctest", ["ctest", "--test-dir", "build", "--output-on-failure"]))
    steps.append(command_result("differential", [sys.executable, "tools/differential.py", "--executable", args.executable, "--count", "100"]))
    steps.append(command_result("stress", [sys.executable, "tools/stress.py", "--executable", args.executable, "--cycles", "1000"]))
    steps.append(command_result("fuzz", [sys.executable, "tools/fuzz.py", "--executable", args.executable, "--cases", "1000"]))
    steps.append(command_result("benchmark", [sys.executable, "tools/benchmark.py", "--executable", args.executable, "--repetitions", "10"]))
    steps.append(command_result("census", [sys.executable, "tools/census.py"]))
    steps.append(command_result("pty", ["ctest", "--test-dir", "build", "--output-on-failure", "-R", "splice_pty_cases"]))
    for source, name in ((ROOT / ".agent-local/differential.json", "differential.json"), (ROOT / ".agent-local/stress.json", "stress.json"),
                         (ROOT / ".agent-local/fuzz.json", "fuzz.json"), (ROOT / ".agent-local/benchmark.json", "benchmark.json"),
                         (ROOT / ".agent-local/census.json", "census.json")):
        copy_json(source, name)
    (EVIDENCE / "ctest.json").write_text(json.dumps(steps[0], indent=2) + "\n")
    (EVIDENCE / "pty.json").write_text(json.dumps(steps[-1], indent=2) + "\n")
    ctest = steps[0]
    ctest_count = len(re.findall(r"\d+/\d+ Test", ctest["stdout"])) if ctest["status"] == 0 else 0
    raw = {}
    for step in steps:
        if step["name"] in {"differential", "stress", "fuzz", "benchmark", "census"}:
            try: raw[step["name"]] = json.loads(step["stdout"].splitlines()[-1])
            except (json.JSONDecodeError, IndexError): raw[step["name"]] = {}
    gates = []
    def gate(gate_id: str, required: int, actual: int, step: dict, reason: str = "") -> None:
        gates.append({"id": gate_id, "required": required, "actual": actual, "command_status": step["status"],
                      "verified": step["status"] == 0 and actual >= required, "reason": reason})
    gate("fresh-build", 8, ctest_count, ctest, "fast local CTest count; platform matrix not run")
    gate("foundation-fast", 8, ctest_count, ctest, "focused local syntax/runtime suite; not the full F01-F30 corpus")
    gate("differential-fast", 100, raw.get("differential", {}).get("executed", 0), steps[1])
    gate("stress-fast", 1000, raw.get("stress", {}).get("completed_cycles") or 0, steps[2])
    gate("fuzz-fast", 1000, raw.get("fuzz", {}).get("executed", 0), steps[3])
    gate("benchmark", 10, raw.get("benchmark", {}).get("repetitions", 0), steps[4], "computed samples are not the 60% target gate")
    gate("private-census", 10000, raw.get("census", {}).get("substantive_production_lines", 0), steps[5], "private ledger is ignored and not published")
    gate("pty-fast", 1, 1 if steps[6]["status"] == 0 else 0, steps[6], "one real PTY session; required campaign is 300")
    pty_heavy_path = ROOT / ".agent-local" / "pty-heavy.json"
    pty_heavy_actual = int(json.loads(pty_heavy_path.read_text()).get("completed_sessions", 0)) if pty_heavy_path.exists() else 0
    pty_heavy_status = 0 if pty_heavy_actual >= 300 and pty_heavy_path.exists() and not json.loads(pty_heavy_path.read_text()).get("failures") else None
    gates.append({"id": "pty-heavy", "required": 300, "actual": pty_heavy_actual, "command_status": pty_heavy_status, "verified": pty_heavy_status == 0 and pty_heavy_actual >= 300, "reason": "recorded independent 300-session campaign" if pty_heavy_status == 0 else "300-session campaign was not run"})
    heavy_concurrency_actual = 0
    concurrency_path = ROOT / ".agent-local" / "concurrency.json"
    if concurrency_path.exists():
        concurrency_data = json.loads(concurrency_path.read_text())
        heavy_concurrency_actual = int(concurrency_data.get("minimum_peak", 0)) * int(concurrency_data.get("repetitions", 0))
    heavy_stress_actual = 0
    stress_heavy_path = ROOT / ".agent-local" / "stress-heavy.json"
    if stress_heavy_path.exists():
        heavy_stress_actual = int(json.loads(stress_heavy_path.read_text()).get("completed_cycles", 0) or 0)
    heavy_concurrency_status = 0 if heavy_concurrency_actual >= 15000 and concurrency_path.exists() and all(run.get("status") == 0 for run in json.loads(concurrency_path.read_text()).get("runs", [])) else None
    heavy_stress_status = 0 if heavy_stress_actual >= 180000 and stress_heavy_path.exists() and json.loads(stress_heavy_path.read_text()).get("status") == 0 else None
    gates.append({"id": "heavy-concurrency", "required": 15000, "actual": heavy_concurrency_actual, "command_status": heavy_concurrency_status, "verified": heavy_concurrency_status == 0 and heavy_concurrency_actual >= 15000, "reason": "recorded independent 500-group campaign" if heavy_concurrency_status == 0 else "bounded smoke observation only; 500 groups x 30 repetitions was not run"})
    gates.append({"id": "heavy-stress", "required": 180000, "actual": heavy_stress_actual, "command_status": heavy_stress_status, "verified": heavy_stress_status == 0 and heavy_stress_actual >= 180000, "reason": "recorded independent 180000-cycle campaign" if heavy_stress_status == 0 else "180000 completed child cycles were not run by the bounded campaign"})
    source_commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=False).stdout.strip()
    acceptance = {"schema": 1, "source_commit": source_commit, "platform": sys.platform, "steps": steps, "derived_gates": gates,
                  "campaign": "bounded-local", "unverified_required_campaigns": ["1200 authored cases", "20000 differential", "10000 malformed", "300 PTY", "500 groups x 30", "180000 stress", "2000000 fuzz", "Linux x86-64"]}
    target = ROOT / "acceptance" / "ACCEPTANCE.json"
    target.write_text(json.dumps(acceptance, indent=2) + "\n")
    manifest = {"schema": 1, "source_commit": source_commit, "files": {}}
    for path in sorted(EVIDENCE.glob("*.json")):
        manifest["files"][str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest["private_files"] = {}
    for path in sorted((ROOT / ".agent-local").glob("*.json")):
        if path.name in {"census.json", "concurrency.json", "pty-heavy.json", "stress-heavy.json"}:
            manifest["private_files"][str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (ROOT / "acceptance" / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    mutation = command_result("verifier-mutations", [sys.executable, "tools/acceptance_test.py"])
    acceptance["steps"].append(mutation)
    target.write_text(json.dumps(acceptance, indent=2) + "\n")
    manifest["acceptance_sha256"] = hashlib.sha256(target.read_bytes()).hexdigest()
    (ROOT / "acceptance" / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"source_commit": source_commit, "verified": sum(gate["verified"] for gate in gates), "gates": len(gates), "campaign": "bounded-local"}))
    return 0 if all(step["status"] == 0 for step in steps) and mutation["status"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
