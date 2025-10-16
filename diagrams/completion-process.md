flowchart TD
  Manifest[validated task manifest] --> Queue[bounded task queue]
  Queue --> Launch[fork and execvp]
  Launch --> Capture[nonblocking output pipes]
  Capture --> Poll[poll and waitpid WNOHANG]
  Poll --> Timeout{deadline exceeded?}
  Timeout -->|yes| Cancel[SIGTERM and status 124]
  Timeout -->|no| Outcome[per-task status and output identity]
  Cancel --> Outcome
  Outcome --> Aggregate[JSON aggregate exit status]
