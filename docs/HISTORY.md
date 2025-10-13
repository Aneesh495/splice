# History persistence

`history::Store` uses a local append log with escaped tabs, newlines, and backslashes. Each record retains the observed wall-clock timestamp, exit status, working directory, and multiline command. Appends use a separate file stream and retention is bounded; once the retention limit is crossed, a temporary rewrite is renamed into place.

A truncated or malformed tail record is ignored during recovery rather than replayed as a partial command. The store has a mutex for concurrent in-process callers and accepts a test path through `SPLICE_HISTORY`. Interactive tests always use a temporary history path. The default path is `~/.splice/history`, and command contents are not emitted by runtime tracing unless a caller explicitly records them.

This is a local privacy model, not encrypted storage. Permissions, cross-process advisory locking, and a SQLite migration remain open portability/product gates.
