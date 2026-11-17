"""Regression checks for the benchmark measurement protocol."""

import subprocess
import unittest
from unittest.mock import patch

import benchmark


class BenchmarkTests(unittest.TestCase):
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
