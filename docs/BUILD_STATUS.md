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
- Native builtins with parent/child execution distinction, parent descriptor save/restore, and stateful `cd`, assignments, export, options, read, printf, and exit paths.
- Single-owner runtime with real `pipe`, `fork`, process-group setup, `dup2`, `execve`, close-on-exec launch error channel, foreground `waitpid`, background job retention, and pipefail aggregation.
- SIGCHLD/SIGWINCH self-pipe bridge with async-signal-safe handlers, retained `jobs`/`wait`/`fg`/`bg`/`disown` operations, and trace event emission.
- Native termios editor with bounded escape decoding, editing/history navigation, bracketed paste negotiation, redraw, EOF, and a dumb-terminal fallback.
- Persistent escaped history store with bounded retention and malformed-tail recovery.
- Independent `splice-lsp` JSON-RPC syntax-check process.
- Bounded native `splice run --max-parallel N --manifest FILE` runner with JSON validation, concurrent child capture, timeout, output caps, and per-task outcomes.
- Reproducible Make entry points, frozen benchmark protocol, differential/fuzz/stress/benchmark drivers, private census, acceptance registry, artifact hashing, and read-only verification scripts.
- Initial documentation, feature registry, focused syntax/runtime/fault/PTY/LSP/task cases, examples, and editable Mermaid diagrams.

## Commands and outcomes

- `git init -b main`: passed.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`: passed on macOS arm64 with AppleClang 21.0.0.21000334.
- `cmake --build build`: passed for `splice` and independent `splice-lsp` with no compiler warnings.
- `ctest --test-dir build --output-on-failure`: passed, 8/8 tests, including 6 syntax cases, 14 real-process cases, 3 launch-failure cases, a real PTY/history lifecycle, an LSP protocol sequence, and 3 bounded task children.
- `make bootstrap`: passed; CMake 4.0.3, Ninja 1.13.2, Python 3.14.2, and Git 2.54.0 were found without modifying user configuration.
- `tools/differential.py --count 20`: passed with 20/20 matches against dash POSIX mode and Bash POSIX mode on the frozen safe corpus.
- `tools/stress.py --cycles 1000`: passed with 1000 completed real external child cycles.
- `tools/fuzz.py --cases 1000`: passed with 500 valid and 500 malformed syntax-only cases executed.
- `tools/benchmark.py --repetitions 10`: computed median dispatch reduction of approximately 24% against Bash and Splice p95 above 5 ms; the 60% and loaded p95 targets are not met.
- `tools/concurrency.py --groups 20`: observed 21 live groups in the bounded smoke workload; the 500 x 30 heavy gate was not run.
- `git diff --check`: passed.

## Incomplete gates

The evidence harness and registry now exist, but the bounded campaign is not the hard acceptance campaign. The required 1,200 authored semantic cases, 20,000 valid differential programs, 10,000 malformed/incomplete programs, 300 PTY sessions, 500 groups in 30 repetitions, 180,000 completed child cycles, 2,000,000 sanitizer executions, Linux x86-64 profile, 10,000 substantive production lines, and hosted-tip verification remain incomplete. Current measured dispatch targets are explicitly missed: approximately 24% median reduction and p95 above 5 ms.

## Next concrete action

Run `make acceptance` to create the bounded evidence record, inspect and repair any verifier/tooling defects, then perform a skeptical source review before deciding which heavy campaigns can run on this macOS arm64 profile. Do not mark unavailable gates as passed.
