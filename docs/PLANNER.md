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

Static plan inspection reports argv and descriptor operations but never claims that an OS open, process, or terminal transition happened. A plan node is a request; runtime trace events distinguish requested, attempted, successful, failed, and canceled operations.

Here-document collection and fully validated descriptor ranges are subsequent runtime gates. The current planner represents a here-document body source without synchronously filling a pipe, so the eventual runtime can choose an unlinked temporary file or a scheduled writer for bodies larger than pipe capacity.
