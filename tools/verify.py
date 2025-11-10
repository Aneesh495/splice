#!/usr/bin/env python3
"""Verify acceptance evidence hashes and derived progress without rerunning workloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def measured_actual(gate_id: str, root: pathlib.Path, acceptance: dict) -> int | None:
    evidence = root / "acceptance" / "evidence"
    if gate_id in {"fresh-build", "foundation-fast"}:
        path = evidence / "ctest.json"
        if not path.exists(): return None
        text = json.loads(path.read_text()).get("stdout", "")
        return len(re.findall(r"\d+/\d+ Test", text))
    if gate_id == "pty-fast":
        path = evidence / "pty.json"
        if not path.exists(): return None
        text = json.loads(path.read_text()).get("stdout", "")
        return len(re.findall(r"\d+/\d+ Test", text))
    if gate_id == "pty-heavy":
        path = root / ".agent-local" / "pty-heavy.json"
        if not path.exists(): return None
        return int(json.loads(path.read_text()).get("completed_sessions", 0) or 0)
    if gate_id == "performance-targets":
        path = root / ".agent-local" / "benchmark.json"
        if not path.exists(): return None
        data = json.loads(path.read_text())
        reduction = data.get("dispatch_reduction_percent")
        p95_values = [row.get("splice", {}).get("p95_ms", 999.0) for row in data.get("workloads", [])]
        return 1 if reduction is not None and reduction >= 60.0 and p95_values and max(p95_values) < 5.0 else 0
    source_map = {"differential-fast": "differential.json", "stress-fast": "stress.json", "fuzz-fast": "fuzz.json",
                  "benchmark": "benchmark.json", "private-census": "census.json"}
    if gate_id in source_map:
        path = evidence / source_map[gate_id]
        if not path.exists(): return None
        data = json.loads(path.read_text())
        keys = {"differential-fast": "executed", "stress-fast": "completed_cycles", "fuzz-fast": "executed",
                "benchmark": "repetitions", "private-census": "substantive_production_lines"}
        value = data.get(keys[gate_id], 0)
        return int(value or 0)
    if gate_id == "heavy-concurrency":
        path = root / ".agent-local" / "concurrency.json"
        if not path.exists(): return None
        data = json.loads(path.read_text())
        return int(data.get("minimum_peak", 0)) * int(data.get("repetitions", 0))
    if gate_id == "heavy-stress":
        path = root / ".agent-local" / "stress-heavy.json"
        if not path.exists(): return None
        return int(json.loads(path.read_text()).get("completed_cycles", 0) or 0)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--acceptance", default=str(ROOT / "acceptance" / "ACCEPTANCE.json"))
    parser.add_argument("--manifest", default=str(ROOT / "acceptance" / "MANIFEST.json"))
    args = parser.parse_args()
    acceptance_path = pathlib.Path(args.acceptance)
    manifest_path = pathlib.Path(args.manifest)
    if not acceptance_path.exists() or not manifest_path.exists():
        print("verify: acceptance evidence is missing", file=sys.stderr)
        return 1
    acceptance = json.loads(acceptance_path.read_text())
    manifest = json.loads(manifest_path.read_text())
    registry_path = ROOT / "acceptance" / "registry.json"
    registry = json.loads(registry_path.read_text()) if registry_path.exists() else {"gates": []}
    failures = []
    current_commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=False).stdout.strip()
    parent_commit = subprocess.run(["git", "rev-parse", "HEAD^"], cwd=ROOT, text=True, capture_output=True, check=False).stdout.strip()
    source_commit = acceptance.get("source_commit", acceptance.get("commit"))
    if source_commit not in {current_commit, parent_commit}:
        failures.append("acceptance source commit is neither HEAD nor the immediate evidence parent")
    elif source_commit == parent_commit:
        changed = subprocess.run(["git", "diff", "--name-only", source_commit, current_commit], cwd=ROOT, text=True, capture_output=True, check=False).stdout.splitlines()
        if any(not (path.startswith("acceptance/") or path == "docs/REVIEW.md") for path in changed):
            failures.append("evidence commit contains non-evidence source changes")
    if manifest.get("source_commit") != source_commit:
        failures.append("manifest source commit does not match acceptance source commit")
        path = ROOT / relative
        if not path.exists():
            failures.append(f"missing artifact: {relative}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            failures.append(f"altered artifact: {relative}")
    for relative, expected in manifest.get("private_files", {}).items():
        path = ROOT / relative
        if not path.exists(): failures.append(f"missing private artifact: {relative}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected: failures.append(f"altered private artifact: {relative}")
    expected_acceptance_hash = manifest.get("acceptance_sha256")
    if not expected_acceptance_hash:
        failures.append("acceptance record hash is missing")
    elif hashlib.sha256(acceptance_path.read_bytes()).hexdigest() != expected_acceptance_hash:
        failures.append("altered acceptance record")
    derived = {gate.get("id"): gate for gate in acceptance.get("derived_gates", [])}
    for required_gate in registry.get("gates", []):
        artifact = required_gate.get("required_artifact")
        if artifact and not (ROOT / artifact).exists():
            failures.append(f"missing required artifact for {required_gate.get('id')}: {artifact}")
        if artifact and artifact.startswith(".agent-local/") and artifact not in manifest.get("private_files", {}):
            failures.append(f"required private artifact is not hashed: {artifact}")
        if artifact and not artifact.startswith(".agent-local/") and artifact.startswith("acceptance/") and artifact not in manifest.get("files", {}):
            failures.append(f"required public artifact is not hashed: {artifact}")
        gate = derived.get(required_gate.get("id"))
        if gate is None:
            failures.append(f"missing derived gate: {required_gate.get('id')}")
            continue
        actual = measured_actual(required_gate.get("id"), ROOT, acceptance)
        if actual is None:
            failures.append(f"missing measurable evidence for {required_gate.get('id')}")
            continue
        expected_verified = gate.get("command_status") == 0 and actual >= int(required_gate.get("minimum_progress", 0))
        if gate.get("actual") != actual:
            failures.append(f"gate actual is not derived for {required_gate.get('id')}: recorded={gate.get('actual')} measured={actual}")
        if gate.get("verified") != expected_verified:
            failures.append(f"gate verdict is not derived for {required_gate.get('id')}")
    for step in acceptance.get("steps", []):
        if step.get("status") != 0:
            failures.append(f"failed evidence step: {step.get('name')}")
    for gate in acceptance.get("derived_gates", []):
        if not gate.get("verified", False):
            failures.append(f"unverified gate: {gate.get('id')}: {gate.get('reason', '')}")
    if failures:
        print("verification failed:")
        print("\n".join(f"- {failure}" for failure in failures))
        return 1
    print("verification passed: all recorded gates and hashes are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
