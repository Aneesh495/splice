#!/usr/bin/env python3
"""Real-process semantic cases for the first runtime boundary."""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile


EXECUTABLE = pathlib.Path(sys.argv[1])


def run(command: str, *, input_data: str = "", cwd: pathlib.Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(EXECUTABLE), "-c", command], input=input_data, text=True,
                          capture_output=True, cwd=cwd, check=False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    plain = run('printf "%s\\n" hello')
    require(plain.returncode == 0 and plain.stdout == "hello\n", "builtin printf failed")

    pipeline = run('printf "%s\\n" hello | tr a-z A-Z')
    require(pipeline.returncode == 0 and pipeline.stdout == "HELLO\n", "real pipeline failed")

    sequence = run('echo first; echo second')
    require(sequence.stdout.splitlines() == ["first", "second"], "sequence ordering failed")

    short_circuit = run('false && echo wrong; true || echo wrong; echo right')
    require(short_circuit.stdout == "right\n", "AND/OR short circuit failed")

    negated = run('! false')
    require(negated.returncode == 0, "negated pipeline status failed")

    pipefail = run('set -o pipefail; false | true')
    require(pipefail.returncode == 1, "pipefail did not preserve failed stage")

    parameters = run('name=splice; printf "[%s]\\n" "$name" "${missing:-fallback}"')
    require(parameters.returncode == 0 and parameters.stdout == "[splice]\n[fallback]\n", "parameter expansion failed")

    substitution = run('printf "result=%s\\n" "$(printf inner)"')
    require(substitution.returncode == 0 and substitution.stdout == "result=inner\n", "command substitution failed")

    background = run('sleep 0.01 & echo foreground')
    require(background.returncode == 0 and background.stdout == "foreground\n", "background launch failed")

    with tempfile.TemporaryDirectory(prefix="splice-runtime-") as directory:
        root = pathlib.Path(directory)
        redirected = run('printf output >file; cat file', cwd=root)
        require(redirected.returncode == 0 and redirected.stdout == "output", "output redirection failed")

        ordered = run('printf out >file 2>&1; cat file', cwd=root)
        require(ordered.stdout == "out", "left-to-right descriptor plan failed")

        reversed_order = run('printf out 2>&1 >file; cat file', cwd=root)
        require(reversed_order.stdout == "out", "reversed descriptor plan did not execute")

        globbed = run('printf "<%s>\\n" *', cwd=root)
        require(globbed.returncode == 0 and "file" in globbed.stdout, "pathname expansion failed")

    directory = run('cd /tmp; pwd')
    require(directory.returncode == 0 and directory.stdout.rstrip() == os.path.realpath("/tmp"), "parent cd builtin failed")

    read_case = run('read value; printf "[%s]\\n" "$value"', input_data="line\n")
    require(read_case.returncode == 0 and read_case.stdout == "[line]\n", "read builtin failed")

    missing = run('definitely-not-a-splice-command')
    require(missing.returncode == 127 and "child launch failed" in missing.stderr, "exec failure status failed")

    print("runtime cases: 14 passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"runtime case failure: {error}", file=sys.stderr)
        raise SystemExit(1)
