#!/usr/bin/env python3
"""Bounded task-runner smoke test with real concurrent children."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


EXECUTABLE = pathlib.Path(sys.argv[1])


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="splice-task-") as directory:
        manifest = pathlib.Path(directory) / "tasks.json"
        manifest.write_text(json.dumps({"tasks": [
            {"id": "one", "argv": ["printf", "one"], "output": "capture"},
            {"id": "two", "argv": ["printf", "two"], "output": "capture"},
            {"id": "bad", "argv": ["splice-task-missing"], "output": "capture"},
        ]}))
        result = subprocess.run([str(EXECUTABLE), "run", "--max-parallel", "2", "--manifest", str(manifest)],
                                text=True, capture_output=True, check=False)
        if result.returncode != 1:
            raise AssertionError(result.stderr)
        report = json.loads(result.stdout)
        by_id = {item["id"]: item for item in report["outcomes"]}
        assert by_id["one"]["stdout"] == "one"
        assert by_id["two"]["stdout"] == "two"
        assert by_id["bad"]["status"] == 127
    print("task cases: 3 real tasks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
