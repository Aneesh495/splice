stateDiagram-v2
  [*] --> Registered: fork and setpgid
  Registered --> Foreground: tcsetpgrp
  Registered --> RunningBackground: return prompt
  Foreground --> Running: child executes
  RunningBackground --> Running: waitpid WNOHANG
  Running --> Stopped: WIFSTOPPED
  Stopped --> Foreground: future fg handoff
  Running --> Completed: every member reaped
  Completed --> Retained: job record preserves status
  Retained --> [*]: bounded pruning policy
