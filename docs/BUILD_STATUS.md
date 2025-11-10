# Build status

Last updated: 2026-10-02

## Repository

- Repository: `splice`
- Branch: `main`
- Remote: `https://github.com/Aneesh495/splice.git`; local and hosted main tips matched at the last publication checkpoint.
- Authenticated GitHub account: `Aneesh495`.
- Workspace was empty and had no ancestor Git repository.

## Implemented

- Native CMake project with C++20, strict warning flags, Ninja-compatible configure/build, optional sanitizer flags.
- `splice --help`, `splice --version`, `-c`, script/stdin input, profile validation, and syntax-only inspection modes.
- Immutable source spans and diagnostics with location and repair suggestions.
- Quote-preserving lexer for words, parameters, arithmetic/command substitutions, operators, IO numbers, and incomplete quotes.
- Typed AST for simple commands, ordered redirections, pipelines, AND/OR chains, sequences, backgrounds, subshells, and groups.
- Expansion state and planner contracts for parameters, assignments, field/pathname expansion, and ordered descriptor operations.
- Native builtins with parent/child execution distinction, transactional readonly-assignment validation, parent descriptor save/restore including originally closed descriptors, and stateful `cd`, assignments, export, options, read, printf, and exit paths.
- Single-owner runtime with real `pipe`, `fork`, process-group setup, `dup2`, `execve`, close-on-exec launch error channel, foreground `waitpid`, background job retention, stage-order pipeline status, pipefail aggregation, isolated subshells, collected here-documents, and `|&` stderr merging.
- SIGCHLD/SIGWINCH self-pipe bridge with async-signal-safe handlers, retained `jobs`/`wait`/`fg`/`bg`/`disown` operations, and trace event emission.
- Native termios editor with bounded escape decoding, editing/history navigation, bracketed paste negotiation, redraw, EOF, and a dumb-terminal fallback.
- Persistent escaped history store with bounded retention and malformed-tail recovery.
- Independent `splice-lsp` JSON-RPC syntax-check process.
- Bounded native `splice run --max-parallel N --manifest FILE` runner with JSON validation, process-group TERM-to-KILL timeout cleanup, explicit `execve` PATH resolution without shell fallback, concurrent child capture, output caps, and per-task outcomes; descendant-specific evidence remains open.
- Reproducible Make entry points, frozen benchmark protocol, differential/fuzz/stress/benchmark drivers, private census, acceptance registry, artifact hashing, and read-only verification scripts.
- Initial documentation, feature registry, focused syntax/runtime/fault/PTY/LSP/task cases, examples, and editable Mermaid diagrams.

## Commands and outcomes

- `git init -b main`: passed.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`: passed on macOS arm64 with AppleClang 21.0.0.21000334.
- `cmake --build build`: passed for `splice` and independent `splice-lsp` with no compiler warnings.
- `ctest --test-dir build --output-on-failure`: passed focused syntax/runtime/fault/PTY/LSP/task checks after semantic repairs; runtime now reports 19 cases and task runner 5 cases. Full 8-test suite remains the fast gate.
- `make bootstrap`: passed; CMake 4.0.3, Ninja 1.13.2, Python 3.14.2, and Git 2.54.0 were found without modifying user configuration.
- `tools/differential.py --count 20`: passed with 20/20 matches against dash POSIX mode and Bash POSIX mode on the frozen safe corpus.
- `tools/stress.py --cycles 1000`: passed with 1000 completed real external child cycles.
- `tools/fuzz.py --cases 1000`: passed with 500 valid and 500 malformed syntax-only cases executed.
- `tools/benchmark.py --repetitions 10`: computed median dispatch reduction of approximately 24% against Bash and Splice p95 above 5 ms; the 60% and loaded p95 targets are not met.
- `tools/concurrency.py --groups 500 --repetitions 30 --hold-seconds 1.0`: passed all 30 repetitions with minimum observed peak 501 live process groups.
- `tools/pty_campaign.py --sessions 300`: passed 300 real PTY sessions with zero failures.
- `tools/stress.py --cycles 180000`: passed 180000 completed real external child cycles in 521.011 seconds through a stdin script transport; persistent zombie occupancy was not sampled.
- `tools/acceptance.py` previously generated bounded evidence at source checkpoint `d8dcd0b`; it is stale after these repairs and must be regenerated after the repair commit. The verifier now rejects stale acceptance records, missing/altered artifacts, false derived thresholds, and unverified required gates.

## Incomplete gates

The bounded campaign and the declared heavy concurrency, PTY, and stress workloads now have positive local evidence on macOS arm64. The original hard gates still incomplete are the 1,200 authored semantic cases, 20,000 distinct differential programs, 10,000 malformed/incomplete programs, 2,000,000 sanitizer executions, Linux x86-64 profile, 10,000 substantive production lines, independent zero-zombie/reaping measurements, and the performance targets. The measured dispatch target remains missed: the benchmark record must be read for the exact current percentage and p95.

## Next concrete action

Commit and publish the semantic repair batch, rerun the bounded acceptance campaign against that source commit, perform a second skeptical review, then commit the evidence snapshot and verify the local/hosted tips. Heavy or unavailable gates must remain explicitly incomplete.

- `git diff --check`: passed after the semantic repair batch.