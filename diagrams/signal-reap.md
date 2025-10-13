sequenceDiagram
  participant Child
  participant Kernel
  participant Handler as SIGCHLD handler
  participant Pipe as self-pipe
  participant Loop as runtime::Runtime
  Child->>Kernel: exit or stop
  Kernel->>Handler: SIGCHLD
  Handler->>Pipe: async-signal-safe write(byte)
  Loop->>Pipe: drain wakeup bytes
  Loop->>Kernel: waitpid(-1, WNOHANG|WUNTRACED|WCONTINUED)
  Kernel-->>Loop: owned status
  Loop->>Loop: update Job and trace event
