# Runtime and status ownership

`runtime::Runtime` is the only owner of child statuses in the current implementation. It creates every pipeline pipe, forks every stage, establishes a process group, closes inherited endpoints, applies the planned descriptor operations, launches external programs with `execve`, and collects foreground outcomes with `waitpid`.

A child setup failure is sent through a close-on-exec error pipe containing an operation and errno. Successful `execve` closes that channel; a launch failure is reported as 126 for permission errors, 127 for lookup/exec-not-found, and 125 for internal setup failure. This keeps an exec failure distinct from a program that returned a normal status.

Foreground pipelines retain one `Job` record with all member PIDs. Completion is declared only when every member has a terminal wait result; stop and continue notifications update state without consuming a result slot. The aggregate uses pipeline stage order, not reap order. `pipefail` selects the rightmost nonzero terminal stage but never discards per-stage results. Background jobs are registered before the runtime returns to the next command, polled with `waitpid(-1, WNOHANG | WUNTRACED | WCONTINUED)`, and drained during runtime cleanup.

The current loop is single-threaded and installs a self-pipe SIGCHLD/SIGWINCH wakeup. Handlers only write a signal byte; the owner drains the pipe and performs wait/status/redraw work in normal process context. Terminal process-group handoff is present for a controlling terminal, and the parent ignores job-control stop signals while restoring its own foreground group.

No shell or `system()` call is used for execution. `PATH` lookup is implemented in Splice and the child calls `execve` directly. Text executables that return `ENOEXEC` are not yet interpreted by Splice and remain a documented compatibility gap.
