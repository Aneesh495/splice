#!/usr/bin/env python3
"""Minimal real-PTY lifecycle test for the native editor and terminal restore."""

from __future__ import annotations

import os
import pathlib
import pty
import select
import subprocess
import tempfile
import time


EXECUTABLE = pathlib.Path(__import__("sys").argv[1])


def read_until(fd: int, needle: bytes, timeout: float = 4.0) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout
    while needle not in data and time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try:
                data.extend(os.read(fd, 4096))
            except OSError:
                break
    if needle not in data:
        raise AssertionError(f"did not observe {needle!r}; got {bytes(data)!r}")
    return bytes(data)


def read_until_all(fd: int, needles: tuple[bytes, ...], timeout: float = 4.0) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout
    while not all(needle in data for needle in needles) and time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try:
                data.extend(os.read(fd, 4096))
            except OSError:
                break
    if not all(needle in data for needle in needles):
        raise AssertionError(f"did not observe {needles!r}; got {bytes(data)!r}")
    return bytes(data)


def main() -> int:
    master, slave = pty.openpty()
    with tempfile.TemporaryDirectory(prefix="splice-pty-") as directory:
        env = os.environ.copy()
        env["SPLICE_HISTORY"] = str(pathlib.Path(directory) / "history")
        process = subprocess.Popen([str(EXECUTABLE)], stdin=slave, stdout=slave, stderr=slave,
                                   env=env, start_new_session=True)
        os.close(slave)
        try:
            read_until(master, b"splice[0]$ ")
            os.write(master, b"printf 'pty-ok\\n'\n")
            output = read_until_all(master, (b"pty-ok", b"splice[0]$ "))
            if b"pty-ok" not in output:
                raise AssertionError(output)
            os.write(master, b"\x04")
            process.wait(timeout=4)
            history = pathlib.Path(directory) / "history"
            if not history.exists() or b"pty-ok" not in history.read_bytes():
                raise AssertionError("interactive history was not persisted")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=2)
            os.close(master)
    print("pty cases: 1 passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
