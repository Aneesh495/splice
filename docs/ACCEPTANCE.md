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

`make acceptance` runs a bounded local campaign and writes `acceptance/ACCEPTANCE.json`, `acceptance/MANIFEST.json`, and ignored raw evidence under `acceptance/evidence/`. It records the tested commit, exact commands, exit codes, durations, parsed progress, and derived gate values. It does not turn a fast run into the brief's full campaign: the required 20,000 differential programs, 10,000 malformed programs, 300 PTY sessions, 500 groups in 30 repetitions, 180,000 child cycles, 2,000,000 sanitizer executions, Linux profile, and 10,000-line private census remain explicit gates.

`make verify` never reruns workloads. It checks the acceptance commit, artifact hashes, required artifact presence, and every derived gate. It intentionally fails while any required gate is missing, stale, altered, or below threshold. The mutation checker generates an isolated altered copy to exercise the verifier without changing recorded evidence.

The private census is written to ignored `.agent-local/census.json`. It contains source paths, exclusions, substantive and physical line counts, the source commit, and a reconstruction rule. It is not public project copy and is not included in acceptance artifact hashes.
