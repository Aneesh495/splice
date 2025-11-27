# Final runtime and acceptance review

Date: 2026-10-02

## Verdict

**NEEDS_CHANGES for the original brief, bounded implementation is coherent.**

The native execution path is real and inspectable for the implemented subset: source bytes become quote-preserving tokens and AST nodes, expansions become ordered plans, plans launch real `fork`/`execve` pipelines, descriptors are applied in order, process groups are tracked, and statuses are collected by one runtime owner. The second and third review passes repaired pipeline stage-order status, subshell isolation, arithmetic precedence, ordinary and large here-documents, `<<-` tab stripping, `|&`, closed-target descriptor opens, readonly assignment validation, nested `command` dispatch, explicit task `execve`, task TERM-to-KILL escalation, trace sequence continuity, and acceptance threshold/hash checks.

The heavy local campaigns also have positive evidence: 500-group concurrency in 30 repetitions with a minimum peak of 501, 300 real PTY sessions with zero failures, and 180,000 completed external child cycles. The final published snapshot is tied to the source commit recorded in `acceptance/ACCEPTANCE.json` through an evidence-only child commit, and the hosted/local branch tips match.

## Remaining material findings

1. **Acceptance verification is intentionally not green.** `tools/verify.py` rejects the final snapshot only for `performance-targets` and `private-census`. The benchmark evidence measures the frozen workload but does not reach the 60% median dispatch reduction and loaded p95 threshold. The private census is below 10,000 substantive production lines. These are recorded failures, not hidden.
2. **Broad semantic coverage remains incomplete.** The registry is a contract, not a claim of F01-F30 or X01-X16 completion. Functions, loops, conditionals, case, traps, arrays, process substitution, full Bash parameter/array rules, complete `set -e` contexts, aliases, completion, vi mode, Unicode display width, and full LSP features remain open.
3. **Reaping evidence is incomplete.** Stress evidence records completed real child cycles, but the required independent terminated-but-unreaped duration, CPU/syscall, occupancy, and zero-zombie campaign was not performed. The runtime path does not use an auto-reaping disposition.
4. **Failure injection and platform breadth are incomplete.** Deterministic syscall fault injection, sanitizer coverage-guided runs, Linux x86-64 validation, descriptor/resource exhaustion campaigns, and full differential/malformed corpus sizes remain unverified.
5. **Interactive product scope is bounded.** The native editor and PTY lifecycle work, but wide-character display policy, completion schemas, redraw under all asynchronous notifications, vi mode, and full stop/resume/trap behavior are not acceptance-complete.

## Evidence review

- `acceptance/ACCEPTANCE.json` derives gate values from raw JSON and command statuses.
- `acceptance/MANIFEST.json` hashes public scrubbed JSON evidence and generated private heavy artifacts.
- `tools/verify.py` recomputes progress from evidence, checks registry thresholds, requires the acceptance-record hash, checks source/manifest identity, and permits only the immediate evidence child commit.
- The benchmark record is a measurement, not a target pass. Raw samples and the exact protocol remain inspectable.

The project should not be described as fully satisfying the initial brief while the two rejected gates and the remaining semantic/platform campaigns are open.
