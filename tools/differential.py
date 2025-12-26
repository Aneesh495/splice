#!/usr/bin/env python3
"""Run a seeded, fixture-confined differential corpus against reference shells."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]


def programs(count: int, seed: int) -> list[str]:
    result = []
    for index in range(count):
        value = f"value_{seed}_{index}"
        choices = [
            f"printf '%s\\n' {value}",
            f"printf '%s\\n' '{value} quoted'",
            f'printf "%s\\n" "{value} double"',
            f"x={value}; printf '%s\\n' \"$x\"",
            f"false && printf wrong_{index}; printf right_{index}",
            f"true || printf wrong_{index}; printf right_{index}",
            "printf x | tr x y",
            f"printf '%s\\n' ${{UNSET:-{value}}}",
            f"printf '%s\\n' \"$(printf nested_{index})\"",
            f"printf '%s\\n' *.definitely-unmatched-{index}",
            f"x={value}; printf '%s\\n' \"${{#x}}\"",
            f"x='prefix_{value}'; printf '%s\\n' \"${{x#prefix_}}\"",
            f"x='{value}_suffix'; printf '%s\\n' \"${{x%_suffix}}\"",
            f"printf '%s\\n' $(( (10 + 20) * 3 ))",
            f"for i in 1 2 3; do printf '%s ' \"$i\"; done; printf '\\n'",
            f"f() {{ printf 'func_%s\\n' \"$1\"; }}; f {value}",
            f"if [ -n '{value}' ]; then printf 'non-empty\\n'; else printf 'empty\\n'; fi",
            f"if [ -z '' ]; then printf 'empty\\n'; else printf 'non-empty\\n'; fi",
            f"x=10; y=20; printf '%s\\n' $(( x + y ))",
            f"printf '%s\\n' ${{SET_VAR:-default_val}}",
            f"x={value}; printf '%s\\n' ${{x:+alternate}}",
            f"x=''; printf '%s\\n' ${{x:-fallback}}",
            f"{{ printf 'group1 '; printf 'group2\\n'; }}",
            f"(printf 'subshell\\n')",
            f"printf '%04d\\n' {index}",
            f"printf 'start '; false || printf 'recovered '; printf 'end\\n'",
            f"x='abc'; y='def'; printf '%s%s\\n' \"$x\" \"$y\"",
            f"printf '%s\\n' 'a b c' | tr ' ' '_'",
            f"i=1; until [ $i -gt 2 ]; do printf '%d ' $i; i=$((i + 1)); done; printf '\\n'",
            f"x={value}; [ \"$x\" = \"{value}\" ] && printf 'equal\\n'",
        ]
        result.append(choices[(seed + index * 7) % len(choices)])
    return result


def run(command: list[str], script: str, cwd: pathlib.Path) -> dict:
    actual = command if command and command[0].endswith("splice") else command + [script]
    result = subprocess.run(actual, text=True, capture_output=True, cwd=cwd,
                            env={**os.environ, "LC_ALL": "C"}, stdin=subprocess.DEVNULL, check=False)
    return {"stdout": result.stdout, "stderr": result.stderr, "status": result.returncode}


def digest(value: dict) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--seed", type=int, default=20261002)
    args = parser.parse_args()
    executable = str(pathlib.Path(args.executable).resolve())
    references = []
    if shutil.which("dash"):
        references.append(("dash-posix", ["dash", "-c"]))
    if shutil.which("bash"):
        references.append(("bash-posix", ["bash", "--posix", "--noprofile", "--norc", "-c"]))
    rows = []
    with tempfile.TemporaryDirectory(prefix="splice-diff-") as directory:
        cwd = pathlib.Path(directory)
        for index, script in enumerate(programs(args.count, args.seed)):
            splice = run([executable, "-c", script], script, cwd)
            refs = {name: run(command, script, cwd) for name, command in references}
            equal = all(splice == reference for reference in refs.values()) if refs else None
            rows.append({"index": index, "script": script, "splice": splice, "references": refs, "equal": equal,
                         "classification": "match" if equal else "reference-difference" if refs else "no-reference",
                         "digest": digest(splice)})
    result = {"schema": 1, "seed": args.seed, "requested": args.count, "executed": len(rows), "references": [name for name, _ in references],
              "matches": sum(row["equal"] is True for row in rows), "differences": sum(row["equal"] is False for row in rows), "cases": rows}
    target = ROOT / ".agent-local" / "differential.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("requested", "executed", "matches", "differences", "references")}))
    return 0 if references and result["differences"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
