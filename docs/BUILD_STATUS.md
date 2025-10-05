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
- Initial documentation, feature registry, focused semantic cases, and editable Mermaid diagrams.

## Commands and outcomes

- `git init -b main`: passed.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`: passed on macOS arm64 with AppleClang 21.0.0.21000334.
- `cmake --build build`: passed after adding syntax, expansion, and plan modules.
- `ctest --test-dir build --output-on-failure`: passed, 3/3 tests, including six individually asserted Python syntax cases.
- `./build/splice --dump-tokens -c 'printf "%s\\n" hello | tr a-z A-Z'`: passed and emitted quote-preserving token JSON.
- `./build/splice --dump-ast -n -c 'echo hi >out 2>&1 && echo done'`: passed and emitted AST JSON.
- `./build/splice --dump-plan -c 'echo hi >out 2>&1'`: passed and emitted open-then-duplicate descriptor actions.
- `git diff --check`: passed.

## Incomplete gates

All real process, interactive, performance, scale, evidence, publication-at-current-tip, and private census gates are incomplete. Foundation syntax and planning are partial only; no feature is marked fully implemented in the registry yet.

## Next concrete action

Implement the single-owner runtime with real `fork`, `execve`, `waitpid`, pipes, process groups, ordered redirections, and parent-safe builtin execution. Wire it into `splice -c` and preserve plan inspection as a no-execute path.
