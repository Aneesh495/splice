#!/usr/bin/env python3
"""Run owned examples through the native executable."""

from __future__ import annotations

import argparse
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    args = parser.parse_args()
    examples = [ROOT / "examples/build-pipeline.sp", ROOT / "examples/search-tree.sp"]
    for example in examples:
        result = subprocess.run([args.executable, str(example)], cwd=ROOT, text=True, capture_output=True, check=False)
        print(f"== {example.name} ==")
        print(result.stdout, end="")
        if result.returncode != 0:
            print(result.stderr)
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
