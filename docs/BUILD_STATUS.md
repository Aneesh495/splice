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
- Initial documentation, feature registry, focused syntax/runtime/fault cases, and editable Mermaid diagrams.

## Commands and outcomes

- `git init -b main`: passed.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`: passed on macOS arm64 with AppleClang 21.0.0.21000334.
- `cmake --build build`: passed after adding the native builtin and runtime modules with no compiler warnings.
- `ctest --test-dir build --output-on-failure`: passed, 5/5 tests, including 6 syntax cases, 14 real-process cases, and 3 launch-failure cases.
- `./build/splice -c 'printf "%s\\n" hello | tr a-z A-Z'`: passed through Splice-owned pipe/fork/exec and produced `HELLO`.
- `./build/splice -c 'set -o pipefail; false | true'`: returned status 1; ordinary `false | true` returned status 0.
- `./build/splice -c 'echo $(printf inner)'`: passed with concurrent captured output and trailing-newline removal.
- `git diff --check`: passed.

## Incomplete gates

All interactive editor, persistent history, language-server, signal-wakeup, advanced language, performance, scale, evidence, publication-at-current-tip, and private census gates are incomplete. The foundation runtime is partial: here-document bodies, `jobs`/`wait`/`fg`/`bg`, traps, ENOEXEC interpretation, complete `set -e` contexts, and descriptor-failure injection are not yet acceptance-complete.

## Next concrete action

Add versioned trace events, SIGCHLD self-pipe wakeups, retained job commands, and a native termios lifecycle. Then exercise stop/resume and terminal restoration through a real PTY harness.
