"""Regression checks for the benchmark measurement protocol."""

import subprocess
import json
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch

import benchmark


class BenchmarkTests(unittest.TestCase):
    @unittest.skipUnless(Path("/bin/bash").is_file(), "Bash reference is required")
    def test_failed_workload_cannot_produce_successful_benchmark_report(self):
        for script, status in (("/usr/bin/false", 1), ("/nonexistent/benchmark-command", 127)):
            with self.subTest(script=script), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "benchmarks").mkdir()
                (root / "benchmarks/protocol.json").write_text(json.dumps({
                    "corpus": [{"id": "failure-fixture", "script": script}]
                }))
                with patch.object(benchmark, "ROOT", root), \
                        patch.object(sys, "argv", [
                            "benchmark.py", "--executable", "/bin/bash",
                            "--repetitions", "1", "--warmup", "0"
                        ]):
                    with self.assertRaisesRegex(RuntimeError, f"status={status}"):
                        benchmark.main()
                self.assertFalse((root / "artifacts/benchmark.json").exists())

    def test_requested_samples_keep_slow_measurements_and_original_order(self):
        durations = [4, 1, 9, 2]
        ticks = []
        for index, duration in enumerate(durations):
            start = index * 20_000_000
            ticks.extend([start, start + duration * 1_000_000])
        result = subprocess.CompletedProcess([], 0, "PROBE", "")
        with patch.object(benchmark.subprocess, "run", return_value=result) as run, \
                patch.object(benchmark.time, "monotonic_ns", side_effect=ticks):
            samples = benchmark.sample(["fixture-shell"], repetitions=4, warmup=2)
        self.assertEqual(run.call_count, 6)
        self.assertEqual(samples, durations)
        summary = benchmark.summarize("fixture-shell", samples)
        self.assertEqual(summary["sample_count"], 4)
        self.assertEqual(summary["median_ms"], 3)
        self.assertEqual(summary["p95_ms"], 9)


if __name__ == "__main__":
    unittest.main()
