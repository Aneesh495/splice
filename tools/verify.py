#!/usr/bin/env python3
"""Verify acceptance evidence hashes and derived progress without rerunning workloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


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
    source_commit = acceptance.get("source_commit", acceptance.get("commit"))
    ancestor_check = subprocess.run(["git", "merge-base", "--is-ancestor", source_commit, current_commit], cwd=ROOT, check=False)
    if ancestor_check.returncode != 0:
        failures.append("acceptance source commit is not an ancestor of HEAD")
    for relative, expected in manifest.get("files", {}).items():
        path = ROOT / relative
        if not path.exists():
            failures.append(f"missing artifact: {relative}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            failures.append(f"altered artifact: {relative}")
    derived = {gate.get("id"): gate for gate in acceptance.get("derived_gates", [])}
    for required_gate in registry.get("gates", []):
        artifact = required_gate.get("required_artifact")
        if artifact and not (ROOT / artifact).exists():
            failures.append(f"missing required artifact for {required_gate.get('id')}: {artifact}")
        gate = derived.get(required_gate.get("id"))
        if gate is None:
            failures.append(f"missing derived gate: {required_gate.get('id')}")
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
