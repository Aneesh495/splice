# Acceptance reproduction

The public entry points are defined in the root `Makefile`:

```sh
make bootstrap
make build
make test
make test-differential
make test-pty
make test-stress
make test-faults
make fuzz
make benchmark
make demo
make acceptance
make verify
```

`make acceptance` runs a bounded local campaign and writes `acceptance/ACCEPTANCE.json`, `acceptance/MANIFEST.json`, and scrubbed JSON evidence under `acceptance/evidence/`. It records the tested source commit, exact commands, exit codes, durations, parsed progress, and derived gate values. The final evidence-only commit may contain only acceptance artifacts and is checked as the source commit's immediate child. It does not turn a fast run into the brief's full campaign: the required 20,000 differential programs, 10,000 malformed programs, 300 PTY sessions, 500 groups in 30 repetitions, 180,000 child cycles, 2,000,000 sanitizer executions, Linux profile, and 10,000-line private census remain explicit gates.

`make verify` never reruns workloads. It checks source and manifest identity, mandatory acceptance-record and artifact hashes, required artifact coverage, recomputed registry thresholds, and every derived gate. It intentionally fails while any required gate is missing, stale, altered, or below threshold. The mutation checker temporarily removes/corrupts an artifact and alters a gate record, runs the verifier, then restores the original bytes; private hashes are included when generated.
