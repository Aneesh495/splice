flowchart LR
  A[cmd >out 2>&1] --> A1[Open fd 1 out] --> A2[Duplicate fd 1 to fd 2]
  B[cmd 2>&1 >out] --> B1[Duplicate fd 1 to fd 2] --> B2[Open fd 1 out]
  A2 --> O1[stdout and stderr file]
  B2 --> O2[stdout file, stderr original stdout]
