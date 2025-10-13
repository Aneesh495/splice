sequenceDiagram
  participant Editor as interactive::LineEditor
  participant Shell as Splice process group
  participant Job as foreground Job PGID
  participant TTY as controlling terminal
  Editor->>TTY: restore saved termios
  Shell->>TTY: tcsetpgrp(Job PGID)
  Job->>TTY: read/write terminal
  Job-->>Shell: waitpid stopped or completed
  Shell->>TTY: tcsetpgrp(Shell PGID)
  Shell->>TTY: restore editor raw mode for next prompt
