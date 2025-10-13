# Interactive lifecycle

Interactive mode starts only when stdin is a controlling terminal in the shell's foreground process group. `interactive::LineEditor` uses `termios` directly and restores the saved attributes on every command boundary, EOF, exception-free shutdown, and editor destruction. It does not link Readline.

The current editor supports insertion, deletion, backspace, left/right/home/end, Ctrl-A, Ctrl-E, Ctrl-U, Ctrl-W, up/down history, Ctrl-C line cancellation, Ctrl-D EOF, bracketed paste mode negotiation, and a dumb-terminal fallback. Cursor movement is byte-based; wide-character and combining-mark display width is not yet claimed. Escape sequences are bounded to eight bytes so a partial sequence cannot block the shell indefinitely.

Before a parsed command executes, raw mode is restored so child programs inherit ordinary terminal behavior. Foreground process groups receive the terminal with `tcsetpgrp`; Splice ignores its own `SIGTTOU`, `SIGTTIN`, and `SIGTSTP` while restoring the shell group. A real PTY test verifies prompt redraw, a builtin command, history persistence to a temporary path, and EOF shutdown.

The SIGCHLD and SIGWINCH handlers only write a byte to the runtime self-pipe. They do not allocate, log, acquire locks, or parse syntax. The owning loop drains that wakeup and performs `waitpid` and redraw work in normal process context.
