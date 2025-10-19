#!/usr/bin/env python3
"""Deterministic syntax-only generated corpus; never executes generated text."""

from __future__ import annotations

import argparse
import json
import pathlib
import random
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def valid(rng: random.Random) -> str:
    words = ["alpha", "beta", "'quoted value'", '"double value"', "$HOME", "${X:-fallback}"]
    operators = [";", " && ", " || ", " | "]
    return rng.choice(words) + rng.choice(operators) + rng.choice(words)


def malformed(rng: random.Random) -> str:
    cases = ["'unterminated", '"unterminated', "echo |", "echo >", "echo ${broken", "( echo"]
    return rng.choice(cases)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    parser.add_argument("--cases", type=int, default=10000)
    parser.add_argument("--seed", type=int, default=20261002)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    valid_count = args.cases // 2
    malformed_count = args.cases - valid_count
    outcomes = {"valid": {"accepted": 0, "rejected": 0}, "malformed": {"accepted": 0, "rejected": 0}}
    classification_failures = 0
    started = time.monotonic_ns()
    for kind, count, maker in (("valid", valid_count, valid), ("malformed", malformed_count, malformed)):
        for _ in range(count):
            source = maker(rng)
            result = subprocess.run([args.executable, "-n", "-c", source], capture_output=True, text=True, check=False)
            accepted = result.returncode == 0
            outcomes[kind]["accepted" if accepted else "rejected"] += 1
            if (kind == "valid" and not accepted) or (kind == "malformed" and accepted):
                classification_failures += 1
    evidence = {"schema": 1, "seed": args.seed, "requested": args.cases, "executed": args.cases,
                "duration_ns": time.monotonic_ns() - started, "outcomes": outcomes,
                "classification_failures": classification_failures,
                "execution_policy": "all generated sources used -n and were confined to parser diagnostics"}
    target = ROOT / ".agent-local" / "fuzz.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence))
    return 0 if classification_failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
