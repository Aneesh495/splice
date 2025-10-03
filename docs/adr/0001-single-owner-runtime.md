# ADR 0001: single owning runtime loop

- Status: accepted
- Date: 2026-10-02

## Decision

Shell state, child registration, SIGCHLD wakeups, foreground waiting, terminal ownership, and prompt-visible status transitions are owned by one event loop. Helper processes may perform bounded ancillary work, but they do not collect shell-owned child statuses.

## Context

A shell can appear concurrent while retaining deterministic ownership. Multiple threads calling `waitpid` create races between foreground waits, command substitutions, asynchronous jobs, and completion helpers. A process that exits between fork and registration is especially easy to lose if signal handling is distributed.

## Consequences

Pipelines and independent process groups remain genuinely concurrent at the operating-system level. The shell's bookkeeping is serialized, which makes status conservation and descriptor cleanup inspectable. The implementation must avoid blocking the loop on output capture and use wakeups for SIGCHLD and input. Throughput claims will be measured rather than inferred from this choice.
