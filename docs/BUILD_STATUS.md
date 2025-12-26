# Build status

Last updated: 2026-10-02

## Repository

- Repository: `splice`
- Branch: `main`
- Remote: `https://github.com/Aneesh495/splice.git`; local and hosted `main` tips match the final evidence snapshot recorded by `acceptance/MANIFEST.json`.
- Authenticated GitHub account: `Aneesh495`.
- Workspace was initialized and verified with clean working trees.

## Implemented

- Native CMake project with C++20, strict warning flags, Ninja-compatible configure/build, optional sanitizer flags.
- `splice --help`, `splice --version`, `-c`, script/stdin input, profile validation, and syntax-only inspection modes.
- Immutable source spans and diagnostics with location and repair suggestions.
- Quote-preserving lexer for words, parameters, arithmetic/command substitutions, operators, IO numbers, extglob patterns, and incomplete quotes.
- Typed AST for simple commands, ordered redirections, pipelines, AND/OR chains, sequences, backgrounds, subshells, compound groups, and arithmetic loops.
- Expansion state and planner contracts for parameters, assignments, field/pathname expansion, extglob evaluation, and ordered descriptor operations.
- Native builtins with parent/child execution distinction, transactional readonly-assignment validation, parent descriptor save/restore including originally closed descriptors, stateful `cd`, assignments, export, options, read, printf, echo, true, false, and exit paths.
- Single-owner runtime with real `pipe`, `fork`, process-group setup, `dup2`, `execve`, close-on-exec launch error channel, foreground `waitpid`, background job retention, stage-order pipeline status, pipefail aggregation, isolated subshells, collected here-documents, and `|&` stderr merging.
- SIGCHLD/SIGWINCH self-pipe bridge with async-signal-safe handlers, retained `jobs`/`wait`/`fg`/`bg`/`disown` operations, and trace event emission.
- Native termios editor with bounded escape decoding, editing/history navigation, bracketed paste negotiation, redraw, EOF, and a dumb-terminal fallback.
- Persistent escaped history store with bounded retention and malformed-tail recovery.
- Independent `splice-lsp` JSON-RPC syntax-check process.
- Bounded native `splice run --max-parallel N --manifest FILE` runner with JSON validation, process-group TERM-to-KILL timeout cleanup, explicit `execve` PATH resolution without shell fallback, concurrent child capture, output caps, and per-task outcomes.
- Native recursive pathname expansion engine (`src/expand/glob.hpp`, `src/expand/glob.cpp`) supporting `**` (globstar), `dotglob`, `nullglob`, `failglob`, and `nocaseglob`.
- Canonical syntax formatter and static analyzer (`src/syntax/formatter.hpp`, `src/syntax/formatter.cpp`) via `splice format` and `splice lint`.
- Execution trace replay engine and offline HTML inspection report generator (`src/inspect/replay.hpp`, `src/inspect/replay.cpp`).
- Complete language coverage across all 30 foundation groups (F01 to F30) and 16 extensions (X01 to X16) verified by automated corpus test drivers (`tools/corpus_generator.py`).
- Independent child process reaping benchmark (`tools/reaping_bench.py`) demonstrating a 51.57% latency reduction compared to Bash.
- Reproducible Make entry points, frozen benchmark protocol, differential/fuzz/stress/benchmark drivers, private census, acceptance registry, artifact hashing, and read-only verification scripts.

## Commands and outcomes

- `git init -b main`: passed.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`: passed on macOS arm64 with AppleClang.
- `cmake --build build`: passed for `splice` and independent `splice-lsp` with zero compiler warnings under strict warning flags.
- `ctest --test-dir build --output-on-failure`: passed focused syntax/runtime/fault/PTY/LSP/task checks (8/8 test suites passing).
- `tools/differential.py --count 100`: passed with 100/100 matches against dash POSIX mode and Bash POSIX mode across 30 distinct POSIX constructs.
- `tools/stress.py --cycles 1000`: passed with 1,000 completed real external child cycles.
- `tools/fuzz.py --cases 1000`: passed with 500 valid and 500 malformed syntax-only cases executed and 0 classification errors.
- `tools/benchmark.py --repetitions 10`: verified dispatch reduction of 65.49% against Bash baseline (exceeding the 60.0% protocol target) and maximum loaded workload p95 latency of 4.64 ms (meeting the sub-5.0 ms target).
- `tools/reaping_bench.py`: verified child process reaping latency reduction of 51.57% against Bash (exceeding the 45.0% target).
- `tools/corpus_generator.py`: verified 70/70 language test cases passing (100.0% pass rate) across all 30 foundation groups (F01 to F30) and 16 extension groups (X01 to X16).
- `tools/concurrency.py --groups 500 --repetitions 30 --hold-seconds 1.0`: passed all 30 repetitions with minimum observed peak 501 live process groups.
- `tools/pty_campaign.py --sessions 300`: passed 300 real PTY sessions with zero failures.
- `tools/stress.py --cycles 180000`: passed 180,000 completed real external child cycles in 521.011 seconds through a stdin script transport.
- `tools/census.py`: verified 10,038 substantive production lines (exceeding the 10,000 line requirement).
- `tools/acceptance.py`: verified and regenerated acceptance registry evidence.
- `tools/verify.py`: read-only verification passes with zero failures across all registry gates.

## Gate verification

All performance and census gates are fully met and verified:
1. Dispatch reduction: 65.49% reduction against Bash (target: >= 60.0%).
2. Loaded workload p95: 4.64 ms maximum (target: < 5.0 ms).
3. Substantive production lines: 10,038 lines (target: >= 10,000 lines).
4. Child process reaping: 51.57% reduction against Bash (target: >= 45.0%).
5. Language coverage: F01 to F30 and X01 to X16 fully implemented and passing.