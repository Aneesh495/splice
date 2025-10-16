# Splice

Splice is an inspectable Unix shell and process runtime. Its implementation boundary is deliberately visible: source bytes become quote-preserving tokens, a typed syntax tree, context-aware expansions, a descriptor plan, real Unix processes, terminal state transitions, and observable statuses.

This repository is being built incrementally from an empty directory. The current checkpoint provides a strict C++20/CMake build, immutable source diagnostics, quote-preserving lexing, a typed AST, context-aware expansion state, JSON token/AST/descriptor-plan inspection, a native fork/exec runtime, a termios editor, history, traces, `splice-lsp`, and a bounded task runner. It does not yet claim shell compatibility or complete acceptance. The live implementation status is in [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md).

## Quick start

Requirements: CMake 3.24 or newer, Ninja, Python 3, and a C++20 compiler.

```sh
make bootstrap
make build
make test
./build/splice -c 'printf "%s\\n" hello'
```

The operational entry points include `make test-differential`, `make test-pty`, `make test-stress`, `make test-faults`, `make fuzz`, `make benchmark`, `make demo`, `make acceptance`, and `make verify`. `make acceptance` writes evidence and `make verify` checks it without rerunning workloads.

Examples live in [`examples/`](examples/) and are executed by `make demo`. The bounded task runner accepts a validated JSON manifest:

```sh
./build/splice run --max-parallel 2 --manifest examples/parallel-tasks.json
```

Runtime inspection is opt-in:

```sh
./build/splice --dump-plan -n -c 'echo hi >out 2>&1'
./build/splice --trace /tmp/splice-trace.jsonl -c 'printf traced'
```

Compatibility claims are recorded in [`docs/LANGUAGE.md`](docs/LANGUAGE.md), not inferred from the project name.

## Design inspection

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) describes module boundaries and ownership.
- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) is the machine-linked language contract.
- [`docs/adr/`](docs/adr/) records consequential implementation choices.
- [`diagrams/`](diagrams/) contains editable Mermaid sources and generated artifacts when available.
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md) records real commands, outcomes, incomplete gates, and the next concrete action.

## Current limitations

The current checkpoint has a usable noninteractive core for simple commands, lists, pipelines, parameter/field/pathname expansion, ordered redirections, parent builtins, and real external process launch. Interactive editing, SIGCHLD wakeups, retained job control, history, traces, LSP syntax checks, and bounded tasks have focused coverage, but the shell is not complete: advanced language forms, full completion/vi/Unicode behavior, full trap and stop/resume semantics, differential and scale campaigns, Linux verification, and the performance targets remain open. No Bash or POSIX conformance claim is made until the feature registry and real-shell tests support it.
