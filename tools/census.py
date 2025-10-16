#!/usr/bin/env python3
"""Count substantive authored production lines with explicit exclusions."""

from __future__ import annotations

import json
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "src", ROOT / "tools")
EXTENSIONS = {".cpp", ".hpp", ".h", ".c", ".py"}
EXCLUDED_PARTS = {"build", "build-debug", "generated", "vendor", "fixtures"}
COMMENT_PREFIXES = ("//", "/*", "*", "*/", "#")


def count_file(path: pathlib.Path) -> tuple[int, int]:
    substantive = 0
    physical = 0
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (UnicodeDecodeError, OSError):
        return 0, 0
    for line in lines:
        physical += 1
        stripped = line.strip()
        if not stripped or stripped.startswith(COMMENT_PREFIXES):
            continue
        substantive += 1
    return substantive, physical


def main() -> int:
    files = []
    for root in SOURCE_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix not in EXTENSIONS or any(part in EXCLUDED_PARTS for part in path.parts):
                continue
            files.append(path)
    rows = []
    total = 0
    physical = 0
    for path in sorted(files):
        substantive, lines = count_file(path)
        total += substantive
        physical += lines
        rows.append({"path": str(path.relative_to(ROOT)), "substantive": substantive, "physical": lines})
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True, check=False).stdout.strip()
    payload = {
        "schema": 1,
        "commit": commit,
        "roots": [str(path.relative_to(ROOT)) for path in SOURCE_ROOTS],
        "extensions": sorted(EXTENSIONS),
        "excluded_parts": sorted(EXCLUDED_PARTS),
        "substantive_production_lines": total,
        "physical_lines": physical,
        "files": rows,
        "reconstruction": "Count nonblank, non-comment lines under src and tools; exclude generated/vendor/fixture paths. Tests, docs, build files, and generated artifacts are excluded from the production total.",
    }
    target = ROOT / ".agent-local" / "census.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(payload, indent=2) + "\n")
    print(json.dumps({key: payload[key] for key in ("commit", "substantive_production_lines", "physical_lines")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
