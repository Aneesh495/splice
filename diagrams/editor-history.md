flowchart LR
  TTY[controlling terminal] --> Editor[interactive::LineEditor]
  Editor --> Buffer[multiline line buffer]
  Editor --> History[history::Store]
  History --> Log[escaped append log]
  Editor --> Parser[syntax::Lexer and Parser]
  Parser --> Runtime[runtime::Runtime]
  Runtime --> Status[exit status and cwd]
  Status --> History
  Runtime --> Redraw[status notification and prompt redraw]
  Redraw --> TTY
