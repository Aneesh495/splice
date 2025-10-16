# Benchmark protocol

`benchmarks/protocol.json` is frozen before optimization. The fast driver measures a subprocess launch through the first exact `PROBE` stdout marker with `LC_ALL=C`, warmup, ten repetitions, median, p95, p99, mean, raw samples, and Bash startup controls. It measures external `/usr/bin/printf` on both sides rather than comparing a builtin with an external.

The current macOS arm64 sample is evidence only and is not a target claim. The initial local run recorded approximately 24 percent median reduction against the pinned Bash invocation, with Splice p95 above 5 ms. The 60 percent reduction and loaded p95 below 5 ms remain unmet. Measurements need a declared Linux x86-64 reference profile, independent launch probe, randomized order, resource limits, and a valid reaping proxy before acceptance.

Zombie measurements must separate waitable-child observation from actual kernel zombie creation. The stress driver reports completed child cycles and explicitly does not claim a zero-zombie observation. A future primary run must record termination-to-reap proxy samples, CPU cost, wait syscall counts, occupancy samples, and Bash comparison without changing the denominator after seeing results.
