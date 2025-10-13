#!/usr/bin/env python3
"""Protocol smoke tests for the independent syntax-only JSON-RPC server."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


EXECUTABLE = pathlib.Path(sys.argv[1])


def frame(message: dict) -> bytes:
    encoded = json.dumps(message).encode()
    return b"Content-Length: " + str(len(encoded)).encode() + b"\r\n\r\n" + encoded


def main() -> int:
    request = b"".join([
        frame({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
        frame({"jsonrpc": "2.0", "id": 2, "method": "splice/check",
               "params": {"uri": "file:///fixture.sp", "text": "echo 'broken"}}),
        frame({"jsonrpc": "2.0", "id": 3, "method": "shutdown", "params": {}}),
        frame({"jsonrpc": "2.0", "method": "exit", "params": {}}),
    ])
    result = subprocess.run([str(EXECUTABLE)], input=request, capture_output=True, check=False)
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode())
    output = result.stdout.decode(errors="replace")
    if "completionProvider" not in output or "unterminated single quote" not in output:
        raise AssertionError(output)
    print("lsp cases: 1 protocol sequence passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
