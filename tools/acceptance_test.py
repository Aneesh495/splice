#!/usr/bin/env python3
"""Exercise the read-only verifier against missing, altered, and gate mutations."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def verify() -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, "tools/verify.py"], cwd=ROOT, text=True, capture_output=True, check=False)


def expect_failure(label: str) -> None:
    result = verify()
    if result.returncode == 0:
        raise AssertionError(f"verifier accepted {label} mutation")


def main() -> int:
    acceptance = ROOT / "acceptance" / "ACCEPTANCE.json"
    manifest = ROOT / "acceptance" / "MANIFEST.json"
    if not acceptance.exists() or not manifest.exists():
        print("acceptance mutation checks: skipped until acceptance has run")
        return 0
    acceptance_bytes = acceptance.read_bytes()
    manifest_data = json.loads(manifest.read_text())
    artifact = ROOT / next(iter(manifest_data["files"]))
    artifact_bytes = artifact.read_bytes()
    baseline = verify().returncode
    try:
        artifact.unlink()
        expect_failure("missing artifact")
        artifact.write_bytes(artifact_bytes + b"corruption")
        expect_failure("altered artifact")
        artifact.write_bytes(artifact_bytes)
        data = json.loads(acceptance_bytes)
        data["derived_gates"][0]["verified"] = False
        acceptance.write_text(json.dumps(data))
        expect_failure("false gate")
    finally:
        artifact.write_bytes(artifact_bytes)
        acceptance.write_bytes(acceptance_bytes)
    if verify().returncode != baseline:
        raise AssertionError("verifier mutation cleanup changed the baseline unexpectedly")
    print("acceptance mutation checks: missing, altered, and false-gate cases rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
