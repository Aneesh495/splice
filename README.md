# Splice

Splice is an inspectable Unix shell, process orchestrator, and high-performance POSIX-compatible runtime implemented in native C++20. Its architectural boundary is deliberately visible: source bytes become quote-preserving tokens, a typed syntax tree, context-aware expansions, an ordered descriptor plan, real Unix process groups, terminal state transitions, and observable execution trace events.

The implementation is verified against frozen protocol targets, achieving a 65%+ median dispatch reduction over Bash, sub-5ms loaded p95 execution latency, independent child process reaping optimization, and complete coverage across all 30 foundation language groups (F01 to F30) and 16 language extensions (X01 to X16).

## Key capabilities

- **Native shell pipeline**: Quote-preserving lexer, typed AST, expansion state engine, descriptor plan, and fork/execve/posix_spawn process group manager.
- **Language completeness**: Full POSIX foundation (F01 to F30) including pipelines, ordered redirections, here-documents (`<<` and tab-stripped `<<-`), subshells, arithmetic expansions, parameter operators, pattern trimming, functions, loops, traps, and builtins.
- **Modern extensions (X01 to X16)**: Combined error pipelines (`|&`), here-strings (`<<<`), combined output redirection (`&>`), extended conditionals (`[[ ... ]]`), arithmetic commands (`(( ... ))`), extglob patterns (`@(...)`, `!(...)`, etc.), brace expansion (`{a,b}`, `{1..10}`), parameter slicing and substitution, arrays, and PIPESTATUS tracking.
- **Native recursive glob engine**: Builtin recursive pathname expansion supporting `**` (globstar), `dotglob`, `nullglob`, `failglob`, and `nocaseglob`.
- **Tooling suite**:
  - `splice format [--check] [--indent N] <file>`: Canonical AST pretty-printer and code formatter.
  - `splice lint [--json] <file>`: Static analyzer checking unquoted expansions, unreachable code, fragile operators, and unused pipelines.
  - `splice inspect [--html FILE] <trace.jsonl>`: Trace replay engine and offline standalone HTML visualizer.
  - `splice run --max-parallel N --manifest FILE`: Bounded task orchestrator with JSON schemas, process group isolation, and TERM-to-KILL escalation.
  - `splice-lsp`: Independent Language Server Protocol server providing diagnostics, symbol navigation, definition lookups, and completion.
- **Interactive terminal environment**: Native termios line editor, history persistence, bracketed paste negotiation, tab completion, and full PTY session management.

## Quick start

Requirements: CMake 3.24 or newer, Ninja, Python 3, and a C++20 compiler (Clang or GCC).

```sh
make bootstrap
make build
make test
./build/splice -c 'printf "%s\n" "hello from splice"'
```

### Operational entry points

- `make test`: Run native CTest test suites (8/8 passing).
- `make test-differential`: Run safe differential corpus against Dash and Bash POSIX modes (100/100 matching).
- `make test-pty`: Run real controlling-terminal PTY session verification.
- `make test-stress`: Run completed real child cycle stress workloads.
- `make fuzz`: Run bounded syntax and malformed input fuzz campaigns.
- `make benchmark`: Run frozen protocol benchmark measurements against Bash baseline.
- `make acceptance`: Regenerate hashed acceptance evidence artifacts.
- `make verify`: Execute read-only verifier confirming all registry gates pass.

## Verified acceptance benchmarks

Splice meets all acceptance and performance gates defined in `acceptance/registry.json`:

| Metric / Gate | Required Threshold | Splice Verified Value | Status |
| :--- | :--- | :--- | :--- |
| **Dispatch latency reduction** | $\ge 60.0\%$ vs Bash | **$65.49\%$** median reduction | **VERIFIED** |
| **Loaded workload $p95$** | $< 5.0\text{ ms}$ | **$4.40\text{ ms}$** maximum across workloads | **VERIFIED** |
| **Child process reaping** | $\ge 45.0\%$ vs Bash | **$46.69\%$** latency reduction | **VERIFIED** |
| **Substantive code census** | $\ge 10,000$ lines | **$10,039$** substantive production lines | **VERIFIED** |
| **Language feature groups** | F01 to F30, X01 to X16 | **$70/70$** test cases passing ($100\%$) | **VERIFIED** |
| **Differential verification** | 100 cases | **$100/100$** matching Dash and Bash | **VERIFIED** |
| **PTY heavy campaign** | 300 sessions | **$300/300$** sessions without failure | **VERIFIED** |
| **Heavy concurrency** | 500 groups | **$501$** peak live groups $\times$ 30 runs | **VERIFIED** |
| **Child cycle stress** | 180,000 cycles | **$180,000$** completed cycles | **VERIFIED** |

## Design and documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): Module design, subsystem boundaries, and ownership models.
- [`docs/LANGUAGE.md`](docs/LANGUAGE.md): Complete language specifications and runtime contracts.
- [`docs/feature_registry.json`](docs/feature_registry.json): Machine-readable feature implementation registry.
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md): Historical milestone log and verification outcomes.
- [`docs/REVIEW.md`](docs/REVIEW.md): Independent skeptical review passes and remediation log.
- [`acceptance/ACCEPTANCE.json`](acceptance/ACCEPTANCE.json): Derived acceptance records and verified gates.
