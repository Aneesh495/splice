flowchart LR
  Source[committed source bytes] --> Build[compiler and build options]
  Build --> Test[semantic and system tests]
  Test --> Raw[raw stdout stderr status traces]
  Raw --> Hash[artifact SHA-256 manifest]
  Hash --> Acceptance[derived ACCEPTANCE.json gates]
  Acceptance --> Verify[read-only verifier]
  Verify -->|missing altered stale| Fail[fail verification]
  Verify -->|all required evidence| Pass[verified subset only]
