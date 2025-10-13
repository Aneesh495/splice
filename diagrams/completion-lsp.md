flowchart LR
  Buffer[editor buffer] --> Complete[bounded completion request]
  Complete --> Schema[declarative schemas and PATH cache]
  Complete --> LSP[splice-lsp JSON-RPC]
  LSP --> Diagnostics[syntax diagnostics]
  Diagnostics --> Editor[non-destructive redraw]
  History[history::Store] --> Editor
