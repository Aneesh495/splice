# Splice

Splice is an inspectable Unix shell and process runtime. Its implementation boundary is deliberately visible: source bytes become quote-preserving tokens, a typed syntax tree, context-aware expansions, a descriptor plan, real Unix processes, terminal state transitions, and observable statuses.

This repository is being built incrementally from an empty directory. The current checkpoint provides a strict C++20/CMake build, immutable source diagnostics, quote-preserving lexing, a typed AST, context-aware expansion state, and JSON token/AST/descriptor-plan inspection. It does not yet claim shell compatibility or complete acceptance. The live implementation status is in [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md).

## Quick start

Requirements: CMake 3.24 or newer, Ninja, and a C++20 compiler.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/splice --help
```

The intended normal invocation is:

```sh
./build/splice -c 'printf "%s\\n" hello'
```

As implementation lands, examples will be executable through Splice itself and linked from [`examples/`](examples/). Compatibility claims are recorded in [`docs/LANGUAGE.md`](docs/LANGUAGE.md), not inferred from the project name.

## Design inspection

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) describes module boundaries and ownership.
- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) is the machine-linked language contract.
- [`docs/adr/`](docs/adr/) records consequential implementation choices.
- [`diagrams/`](diagrams/) contains editable Mermaid sources and generated artifacts when available.
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md) records real commands, outcomes, incomplete gates, and the next concrete action.

## Current limitations

The current checkpoint is still not a usable shell: real process launch, status ownership, interactive terminal control, editor/history, language tools, evidence campaigns, and Linux verification are staged behind the next increments. The syntax-only tools never execute command substitutions or redirections. No Bash or POSIX conformance claim is made until the feature registry and real-shell tests support it.
