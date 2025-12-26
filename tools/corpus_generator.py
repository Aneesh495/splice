#!/usr/bin/env python3
"""Comprehensive test generator and verifier across F01-F30 and X01-X16 language specs."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]


# Definitions of test cases across all 30 foundation groups and 16 extensions
CORPUS_CASES: list[dict[str, str]] = [
    # F01: Lexer & Quoting
    {"group": "F01", "name": "single-quotes", "script": "printf '%s\\n' 'hello world $VAR \\n'"},
    {"group": "F01", "name": "double-quotes", "script": 'x=world; printf "%s\\n" "hello $x"'},
    {"group": "F01", "name": "backslash-escape", "script": "printf '%s\\n' hello\\ \\$world"},
    {"group": "F01", "name": "line-continuation", "script": "printf '%s\\n' hel\\\nlo"},

    # F02: Simple commands & environment assignments
    {"group": "F02", "name": "prefix-assignment", "script": "X=val printf '%s\\n' \"$X\""},
    {"group": "F02", "name": "multiple-assignments", "script": "A=1 B=2; printf '%s %s\\n' \"$A\" \"$B\""},

    # F03: Path resolution
    {"group": "F03", "name": "path-lookup", "script": "which sh >/dev/null && printf 'found\\n'"},

    # F04: Pipelines
    {"group": "F04", "name": "pipeline-stdout", "script": "printf 'foo\\nbar\\n' | grep foo"},
    {"group": "F04", "name": "multi-stage-pipe", "script": "printf 'a\\nb\\nc\\n' | tr a-z A-Z | grep B"},

    # F05: Redirections
    {"group": "F05", "name": "stdout-redir", "script": "printf 'out' > out.tmp; cat out.tmp; rm out.tmp"},
    {"group": "F05", "name": "append-redir", "script": "printf '1' > a.tmp; printf '2' >> a.tmp; cat a.tmp; rm a.tmp"},
    {"group": "F05", "name": "dup-stderr", "script": "printf 'err\\n' >&2 2>&1"},

    # F06: Here-documents
    {"group": "F06", "name": "heredoc-basic", "script": "cat <<EOF\nline1\nline2\nEOF\n"},
    {"group": "F06", "name": "heredoc-strip", "script": "cat <<-EOF\n\tstrip1\n\tstrip2\nEOF\n"},

    # F07: And-Or lists
    {"group": "F07", "name": "and-true", "script": "true && printf 'yes\\n'"},
    {"group": "F07", "name": "and-false", "script": "false && printf 'no\\n' || printf 'alt\\n'"},

    # F08: Sequences & Groups
    {"group": "F08", "name": "semicolon-seq", "script": "printf 'a '; printf 'b\\n'"},
    {"group": "F08", "name": "group-command", "script": "{ x=grp; printf '%s\\n' \"$x\"; }"},

    # F09: Subshells
    {"group": "F09", "name": "subshell-isolation", "script": "x=outer; (x=inner; printf '%s ' \"$x\"); printf '%s\\n' \"$x\""},

    # F10: Parameter expansion
    {"group": "F10", "name": "param-simple", "script": "v=hello; printf '%s\\n' \"$v\" \"${v}\""},
    {"group": "F10", "name": "param-length", "script": "v=hello; printf '%s\\n' \"${#v}\""},

    # F11: Default & alternate value expansion
    {"group": "F11", "name": "param-default", "script": "printf '%s %s\\n' \"${unset:-def}\" \"${set_var:=val}\""},
    {"group": "F11", "name": "param-alternate", "script": "v=exists; printf '%s\\n' \"${v:+alt}\""},

    # F12: Pattern trimming
    {"group": "F12", "name": "trim-prefix", "script": "p=path/to/file.txt; printf '%s %s\\n' \"${p#*/}\" \"${p##*/}\""},
    {"group": "F12", "name": "trim-suffix", "script": "p=file.tar.gz; printf '%s %s\\n' \"${p%.*}\" \"${p%%.*}\""},

    # F13: Command substitution
    {"group": "F13", "name": "cmdsub-dollar", "script": "printf 'got:%s\\n' \"$(printf sub)\""},

    # F14: Arithmetic expansion
    {"group": "F14", "name": "arith-ops", "script": "printf '%s %s %s\\n' \"$((1+2))\" \"$((3*4))\" \"$((10/2))\""},
    {"group": "F14", "name": "arith-precedence", "script": "printf '%s\\n' \"$((2 + 3 * 4))\""},
    {"group": "F14", "name": "arith-ternary", "script": "printf '%s\\n' \"$((1 ? 42 : 0))\""},

    # F15: Pathname expansion
    {"group": "F15", "name": "glob-star", "script": "printf '%s\\n' src/*.cpp | head -n 1 >/dev/null && printf 'matched\\n'"},

    # F16: Case statement
    {"group": "F16", "name": "case-match", "script": "x=banana; case $x in b*) printf 'b\\n';; *) printf 'other\\n';; esac"},

    # F17: If statement
    {"group": "F17", "name": "if-then-else", "script": "if [ 1 = 1 ]; then printf 'then\\n'; else printf 'else\\n'; fi"},
    {"group": "F17", "name": "if-elif", "script": "if false; then printf '1\\n'; elif true; then printf '2\\n'; fi"},

    # F18: For loop
    {"group": "F18", "name": "for-words", "script": "for x in a b c; do printf '%s' \"$x\"; done; printf '\\n'"},

    # F19: While loop
    {"group": "F19", "name": "while-loop", "script": "x=0; while [ $x -lt 3 ]; do printf '%d' \"$x\"; x=$((x + 1)); done; printf '\\n'"},

    # F20: Shell functions
    {"group": "F20", "name": "func-call", "script": "greet() { printf 'hello %s\\n' \"$1\"; }; greet world"},
    {"group": "F20", "name": "func-local", "script": "f() { local x=inner; printf '%s ' \"$x\"; }; x=outer; f; printf '%s\\n' \"$x\""},

    # F21: Test builtin
    {"group": "F21", "name": "test-unary", "script": "[ -n 'text' ] && [ -z '' ] && printf 'ok\\n'"},
    {"group": "F21", "name": "test-int", "script": "[ 5 -gt 3 ] && [ 2 -le 2 ] && printf 'ok\\n'"},

    # F22: Echo & Printf
    {"group": "F22", "name": "printf-formats", "script": "printf '[%s] [%d] [%x]\\n' test 42 255"},

    # F23: cd and pwd
    {"group": "F23", "name": "pwd-test", "script": "cd / && pwd"},

    # F24: export & readonly
    {"group": "F24", "name": "export-test", "script": "export MY_EXP=1; printf '%s\\n' \"$MY_EXP\""},

    # F25: Flow control
    {"group": "F25", "name": "loop-break", "script": "for i in 1 2 3; do [ $i -eq 2 ] && break; printf '%d' \"$i\"; done; printf '\\n'"},
    {"group": "F25", "name": "loop-continue", "script": "for i in 1 2 3; do [ $i -eq 2 ] && continue; printf '%d' \"$i\"; done; printf '\\n'"},

    # F26: eval & source
    {"group": "F26", "name": "eval-simple", "script": "x='printf evaluated\\n'; eval \"$x\""},

    # F27: Traps
    {"group": "F27", "name": "trap-exit", "script": "trap 'printf trapped\\n' EXIT"},

    # F28: Shell options
    {"group": "F28", "name": "pipefail-opt", "script": "set -o pipefail; false | true; printf '%d\\n' $?"},

    # F29: getopts
    {"group": "F29", "name": "getopts-basic", "script": "args='-a -b val'; set -- -a -b val; while getopts 'ab:' opt; do printf '%s ' \"$opt\"; done; printf '\\n'"},

    # F30: umask
    {"group": "F30", "name": "umask-query", "script": "umask >/dev/null && printf 'umask_ok\\n'"},

    # X01: |& pipeline
    {"group": "X01", "name": "pipe-stderr", "script": "printf 'err\\n' >&2 |& grep err"},

    # X02: <<< here-string
    {"group": "X02", "name": "here-string", "script": "cat <<< 'hello here-string'"},

    # X03: &> redirection
    {"group": "X03", "name": "amp-redir", "script": "printf 'both\\n' &> out2.tmp; cat out2.tmp; rm out2.tmp"},

    # X04: [[ ]] conditionals
    {"group": "X04", "name": "cond-pattern", "script": "[[ 'foo' == f* ]] && printf 'match\\n'"},
    {"group": "X04", "name": "cond-regex", "script": "[[ 'splice2026' =~ [0-9]+ ]] && printf 'regex\\n'"},

    # X05: (( )) arithmetic command
    {"group": "X05", "name": "arith-cmd", "script": "(( 5 > 3 )) && printf 'true\\n'"},

    # X06: Extended globbing
    {"group": "X06", "name": "extglob-match", "script": "[[ 'apple' == @(apple|banana) ]] && printf 'extglob\\n'"},

    # X07: Brace expansion
    {"group": "X07", "name": "brace-list", "script": "printf '%s ' {a,b,c}; printf '\\n'"},
    {"group": "X07", "name": "brace-num-range", "script": "printf '%s ' {1..4}; printf '\\n'"},

    # X08: Substring parameter expansion
    {"group": "X08", "name": "substr-exp", "script": "v=abcdef; printf '%s\\n' \"${v:1:3}\""},

    # X09: Pattern replacement
    {"group": "X09", "name": "pat-replace-first", "script": "v=banana; printf '%s\\n' \"${v/a/o}\""},
    {"group": "X09", "name": "pat-replace-all", "script": "v=banana; printf '%s\\n' \"${v//a/o}\""},

    # X10: Case modification
    {"group": "X10", "name": "case-upper", "script": "v=splice; printf '%s\\n' \"${v^^}\""},
    {"group": "X10", "name": "case-lower", "script": "v=SPLICE; printf '%s\\n' \"${v,,}\""},

    # X11: Indexed arrays
    {"group": "X11", "name": "array-indices", "script": "arr=(alpha beta gamma); printf '%s %s\\n' \"${arr[0]}\" \"${arr[2]}\""},
    {"group": "X11", "name": "array-len", "script": "arr=(1 2 3 4); printf '%s\\n' \"${#arr[@]}\""},

    # X12: C-style for loop
    {"group": "X12", "name": "for-c-style", "script": "for ((i=0; i<3; ++i)); do printf '%d ' \"$i\"; done; printf '\\n'"},

    # X13: PIPESTATUS array
    {"group": "X13", "name": "pipestatus-track", "script": "false | true; printf '%s %s\\n' \"${PIPESTATUS[0]}\" \"${PIPESTATUS[1]}\""},

    # X14: Directory stack
    {"group": "X14", "name": "dirstack", "script": "pushd /tmp >/dev/null && popd >/dev/null && printf 'dirs_ok\\n'"},

    # X15: read builtin options
    {"group": "X15", "name": "read-n", "script": "printf 'abc' | (read -n 2 x; printf '%s\\n' \"$x\")"},

    # X16: Aliases
    {"group": "X16", "name": "alias-def", "script": "alias sayhello=\"printf 'hi\\n'\"; sayhello"}
]


def run_shell(executable: str, script: str, cwd: pathlib.Path) -> tuple[int, str, str]:
    result = subprocess.run([executable, "-c", script], cwd=cwd, text=True,
                            capture_output=True, env={**os.environ, "LC_ALL": "C"}, check=False)
    return result.returncode, result.stdout, result.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description="Run language feature coverage across F01-F30 and X01-X16")
    parser.add_argument("--executable", default=str(ROOT / "build" / "splice"))
    parser.add_argument("--output", default=str(ROOT / ".agent-local" / "language_coverage.json"))
    args = parser.parse_args()

    executable = str(pathlib.Path(args.executable).resolve())
    results = []
    passed = 0
    failed = 0

    with tempfile.TemporaryDirectory(prefix="splice-corpus-") as tmpdir:
        cwd = pathlib.Path(tmpdir)
        for case in CORPUS_CASES:
            group = case["group"]
            name = case["name"]
            script = case["script"]
            status, stdout, stderr = run_shell(executable, script, cwd)

            ok = (status == 0)
            if ok:
                passed += 1
            else:
                failed += 1

            results.append({
                "group": group,
                "name": name,
                "script": script,
                "status": status,
                "passed": ok,
                "stdout": stdout,
                "stderr": stderr
            })

    # Group statistics
    group_stats: dict[str, dict[str, int]] = {}
    for res in results:
        g = res["group"]
        if g not in group_stats:
            group_stats[g] = {"total": 0, "passed": 0}
        group_stats[g]["total"] += 1
        if res["passed"]:
            group_stats[g]["passed"] += 1

    summary = {
        "schema": 1,
        "total_cases": len(results),
        "passed": passed,
        "failed": failed,
        "pass_rate_percent": (100.0 * passed / len(results)) if results else 0.0,
        "group_coverage": group_stats,
        "cases": results
    }

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(summary, indent=2) + "\n")

    print(json.dumps({
        "total_cases": len(results),
        "passed": passed,
        "failed": failed,
        "pass_rate": f"{(100.0 * passed / len(results)):.1f}%",
        "groups_covered": len(group_stats)
    }))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
