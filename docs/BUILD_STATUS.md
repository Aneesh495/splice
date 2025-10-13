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
- Initial documentation, feature registry, focused syntax/runtime/fault/PTY/LSP/task cases, and editable Mermaid diagrams.

## Commands and outcomes

- `git init -b main`: passed.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`: passed on macOS arm64 with AppleClang 21.0.0.21000334.
- `cmake --build build`: passed for `splice` and independent `splice-lsp` with no compiler warnings.
- `ctest --test-dir build --output-on-failure`: passed, 8/8 tests, including 6 syntax cases, 14 real-process cases, 3 launch-failure cases, a real PTY/history lifecycle, an LSP protocol sequence, and 3 bounded task children.
- `./build/splice --trace FILE -c 'echo traced | tr a-z A-Z'`: passed and emitted four version-1 JSONL events with monotonic sequence IDs.
- `./build/splice -c 'sleep 0.02 & jobs; wait'`: passed with a retained running job record and collected status.
- `./build/splice run --max-parallel 2 --manifest tasks.json`: passed the bounded runner case with per-task output and 127 lookup status.
- `git diff --check`: passed.

## Incomplete gates

The editor/history/trace/LSP/task-runner core paths now have focused checks, but the full interactive and language-product requirements remain incomplete: vi mode, wide-character display policy, completion schemas, hover/symbol/navigation, concurrent cross-process history locking, full trap and stop/resume semantics, and offline trace report are pending. Advanced language, differential, large-scale process, performance, evidence, publication-at-current-tip, and private census gates are incomplete.

## Next concrete action

Add the evidence harness and executable gate registry, then expand semantic cases and real PTY/fault/stress campaigns. Keep all measured targets and missing-platform claims explicit rather than treating the fast suite as acceptance.
