# ADR 0002: narrow POSIX launch boundary

- Status: accepted
- Date: 2026-10-02

## Decision

All native process and descriptor operations go through the runtime implementation, with platform-specific behavior kept at the POSIX call boundary. External commands are launched with `fork` and `execve`; Splice never invokes Bash, `/bin/sh`, `system()`, or a command tokenizer as its execution engine.

## Context

The shell language has to remain inspectable from source through descriptors and statuses. Delegating the interesting path would make process ownership, redirection order, and failure cleanup unobservable and would make platform differences accidental.

## Consequences

The child setup path must remain small and avoid inherited library locks. Launch failures need an async-safe error channel. macOS and Linux can share the plan/runtime contracts while differing in terminal and anonymous-file fast paths. An optional future `posix_spawn` path must preserve the same plan and status invariants rather than replacing the required fork/exec path.
