flowchart TD
  P[ExecutionPlan] --> C1[Stage 1 descriptor actions]
  P --> C2[Stage 2 descriptor actions]
  Parent[Runtime parent] --> Pipe[Owned pipe endpoints]
  Pipe --> C1
  Pipe --> C2
  C1 --> Close1[Close inherited unused ends]
  C2 --> Close2[Close inherited unused ends]
  Parent --> Restore[Restore saved parent-builtin descriptors]
  Restore --> Parent
