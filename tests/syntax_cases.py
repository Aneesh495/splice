#!/usr/bin/env python3
"""Focused boundary tests for Splice's syntax-only executable."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


EXECUTABLE = pathlib.Path(sys.argv[1])


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(EXECUTABLE), *args], text=True, capture_output=True, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    token_result = run("--dump-tokens", "-c", "echo 'a b' \"$HOME\"; printf x")
    require(token_result.returncode == 0, token_result.stderr)
    tokens = json.loads(token_result.stdout)
    word_parts = tokens[1]["word"]["parts"]
    require(any(part["kind"] == "single-quoted" for part in word_parts), "single quote provenance lost")
    require(any(token["kind"] == "semicolon" for token in tokens), "semicolon token missing")

    ast_result = run("--dump-ast", "-n", "-c", "printf x | tr x y && echo done")
    require(ast_result.returncode == 0, ast_result.stderr)
    ast = json.loads(ast_result.stdout)
    require(ast["commands"][0]["kind"] == "and-or", "AND/OR AST node missing")
    require(ast["commands"][0]["children"][0]["kind"] == "pipeline", "pipeline AST node missing")

    plan_result = run("--dump-plan", "-c", "echo hi >out 2>&1")
    require(plan_result.returncode == 0, plan_result.stderr)
    plan = json.loads(plan_result.stdout)
    descriptors = plan["stages"][0]["descriptors"]
    require([item["kind"] for item in descriptors] == ["open", "duplicate"], "redirection order not preserved")

    invalid = run("-n", "-c", "echo 'unterminated")
    require(invalid.returncode == 2, "unterminated quote was accepted")
    require("unterminated single quote" in invalid.stderr, "quote diagnostic missing")

    incomplete = run("-n", "-c", "echo one |")
    require(incomplete.returncode == 2, "incomplete pipeline was accepted")
    require("incomplete" in incomplete.stderr, "incomplete-input diagnostic missing")

    invalid_redirection = run("-n", "-c", "echo hi >")
    require(invalid_redirection.returncode == 2, "missing redirection operand was accepted")
    require("redirection" in invalid_redirection.stderr, "redirection diagnostic missing")

    print("syntax cases: 6 passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, json.JSONDecodeError) as error:
        print(f"syntax case failure: {error}", file=sys.stderr)
        raise SystemExit(1)
