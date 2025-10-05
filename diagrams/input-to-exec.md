sequenceDiagram
  participant Input as SourceBuffer
  participant Lexer as syntax::Lexer
  participant Parser as syntax::Parser
  participant Expand as expand::Expander
  participant Plan as plan::PlanBuilder
  participant Runtime as runtime::Runtime
  participant OS as POSIX kernel
  Input->>Lexer: bytes and immutable offsets
  Lexer->>Parser: Token with WordPart provenance
  Parser->>Expand: typed Command and Word
  Expand->>Plan: fields and context errors
  Plan->>Runtime: ExecutionPlan
  Runtime->>OS: pipe, fork, setpgid, dup2, execve
  OS-->>Runtime: waitable status
  Runtime-->>Input: observable status and trace link
