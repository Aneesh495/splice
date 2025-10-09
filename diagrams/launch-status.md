sequenceDiagram
  participant Loop as runtime::Runtime
  participant Parent as parent process
  participant Child as pipeline child
  participant Kernel as kernel
  Loop->>Kernel: pipe and close-on-exec error channel
  Loop->>Kernel: fork
  Kernel-->>Child: child context
  Child->>Kernel: setpgid and dup2
  Child->>Kernel: execve(argv, envp)
  Kernel-->>Loop: waitpid status
  Child-->>Loop: error channel only on failed setup/exec
  Loop->>Loop: retain ProcessResult and aggregate Job status
