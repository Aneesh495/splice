# Runtime trace schema

`--trace FILE` writes JSONL events from `inspect::Trace`. Every event has schema version 1, a monotonic sequence ID, a monotonic nanosecond timestamp, a kind, and string fields. Current runtime events include `runtime-created`, `pipeline-registered`, and `child-status` with job, process-group, PID, stage count, and decoded status fields.

The trace is observational. A `pipeline-registered` event proves that the runtime registered process IDs, not that every stage successfully executed. `child-status` is emitted only after the single owner has collected a waitable status. Missing or truncated trace lines must be treated as incomplete evidence by the eventual acceptance verifier.

The writer serializes under a mutex for bounded helper use, but signal handlers never call it. Trace paths are opt-in and no environment or credential values are automatically recorded.
