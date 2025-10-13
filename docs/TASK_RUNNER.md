# Bounded task runner

The Splice extension command is:

```sh
splice run --max-parallel 2 --manifest tasks.json
```

A manifest contains a non-empty `tasks` array. Each task requires an `id` and argv, and may set `cwd`, string environment overrides, `timeout_ms`, and `output` as `capture`, `inherit`, or `discard`. The native runner validates the complete manifest before launching, starts at most the declared number of real child processes, drains captured stdout/stderr through nonblocking descriptors, caps each captured stream at 1 MiB, terminates timed-out tasks, and emits per-task status and duration JSON.

Tasks use `execvp` directly. They are not implicitly rerun after a process restart, and a nonzero task causes the command's aggregate status to be 1 while preserving every individual outcome. Shell syntax is intentionally not accepted as argv; an explicitly marked shell-script task is a later profile gate.
