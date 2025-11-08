#!/usr/bin/env python3
"""Run repeated real controlling-terminal sessions with readiness synchronization."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import pty
import select
import subprocess
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_until_all(fd: int, needles: tuple[bytes, ...], timeout: float) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout
    while not all(needle in data for needle in needles) and time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if ready:
            try: data.extend(os.read(fd, 4096))
            except OSError: break
    if not all(needle in data for needle in needles):
        raise TimeoutError(f"missing PTY markers {needles!r}")
    return bytes(data)


def session(executable: str, number: int) -> dict:
    master, slave = pty.openpty()
    with tempfile.TemporaryDirectory(prefix="splice-pty-campaign-") as directory:
        env = os.environ.copy()
        env["SPLICE_HISTORY"] = str(pathlib.Path(directory) / "history")
        process = subprocess.Popen([executable], stdin=slave, stdout=slave, stderr=slave, env=env, start_new_session=True)
        os.close(slave)
        marker = f"pty-campaign-{number}".encode()
        try:
            read_until_all(master, (b"splice[0]$ ",), 4.0)
            os.write(master, b"printf '" + marker + b"\\n'\n")
            read_until_all(master, (marker, b"splice[0]$ "), 4.0)
            os.write(master, b"\x04")
            process.wait(timeout=4)
            return {"session": number, "status": process.returncode, "marker": marker.decode()}
        except Exception as error:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=1)
                except subprocess.TimeoutExpired: process.kill()
            return {"session": number, "status": 125, "error": str(error)}
        finally:
            os.close(master)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", default=str(ROOT / "build/splice"))
    parser.add_argument("--sessions", type=int, default=300)
    args = parser.parse_args()
    executable = str(pathlib.Path(args.executable).resolve())
    started = time.monotonic_ns()
    rows = [session(executable, index) for index in range(args.sessions)]
    evidence = {"schema": 1, "requested_sessions": args.sessions, "completed_sessions": sum(row["status"] == 0 for row in rows),
                "failures": [row for row in rows if row["status"] != 0], "duration_ns": time.monotonic_ns() - started,
                "transport": "pty.openpty with prompt and output readiness markers"}
    target = ROOT / ".agent-local" / "pty-heavy.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"requested_sessions": args.sessions, "completed_sessions": evidence["completed_sessions"], "failures": len(evidence["failures"])}))
    return 0 if not evidence["failures"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
