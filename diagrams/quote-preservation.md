flowchart TD
  W[syntax::Word span and spelling] --> L[Literal part]
  W --> SQ[SingleQuoted part]
  W --> DQ[DoubleQuoted part]
  W --> E[Escaped part]
  W --> P[Parameter part]
  W --> A[Arithmetic part]
  W --> C[CommandSubstitution part]
  L --> X[expand::Expander context]
  SQ --> X
  DQ --> X
  E --> X
  P --> X
  A --> X
  C --> X
  X --> F[Field vector with quoted provenance]
