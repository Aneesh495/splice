# Descriptor planner

`plan::PlanBuilder` is the boundary between expanded words and OS launch. It produces `ExecutionPlan` values with one `PlannedCommand` per pipeline stage, an argv vector, an environment snapshot, prefix assignments, and an ordered descriptor action list.

Descriptor action order is not normalized. For example:

```text
printf x >out 2>&1
```

plans an output open on descriptor 1 followed by a duplicate of descriptor 1 onto descriptor 2. The reversed form:

```text
printf x 2>&1 >out
```

plans the duplicate first and the output open second, so stderr retains the original stdout. The runtime applies the plan left to right in the child or in a saved parent-builtin descriptor scope.

Static plan inspection reports argv and descriptor operations but never claims that an OS open, process, or terminal transition happened. A plan node is a request; the current runtime trace records registration and terminal child-status transitions, while finer descriptor-attempt and terminal-handoff events remain an inspection gate.

Here-document collection records the delimiter-terminated body in the syntax redirection node. Runtime delivery uses an unlinked temporary file, so a body larger than pipe capacity does not block launch. Quoted delimiter expansion semantics and fully validated descriptor ranges remain profile gates.
