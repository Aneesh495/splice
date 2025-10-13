# Syntax language server

`splice-lsp` is an independent JSON-RPC-over-stdio process. It does not source a workspace document. Its current protocol supports `initialize`, `splice/check`, `shutdown`, and `exit`; `splice/check` invokes the same lexer/parser diagnostics used by the shell and returns LSP-shaped ranges.

The current capability response advertises completion transport only as a contract placeholder; completion, hover, symbols, and definition navigation remain incomplete. Malformed JSON-RPC framing is not executed as shell input. The parser is bounded by the same source buffer and diagnostic policies as `splice --dump-ast`.

The protocol test sends framed initialize/check/shutdown/exit messages and verifies an unterminated-quote diagnostic without executing the supplied text.
