# Final runtime and acceptance status review

The change hardens here-document collection, task timeout cleanup, native launch unwinding, and acceptance evidence measurement. The clean Debug build and all eight focused CTest targets pass; targeted probes also verify `<<-` tab stripping, TERM-to-KILL timeout escalation, corrected CTest/PTY measurement, and the error-pipe allocation cleanup. **confirmed** Two behavioral gaps remain: `wait` can still block after it receives a stop notification, and multiple here-documents still bind the later body incorrectly. **confirmed** Resource-exhaustion restoration, fork-failure cleanup, captured-stream closure, and private-evidence coverage also retain material gaps.

Watch for: **confirmed** stopped-job `wait` hang at `src/runtime/runtime.cpp:375-387`; **confirmed** multiple-heredoc misbinding at `src/syntax/parser.cpp:90-132`; **confirmed** parent descriptor and partial-launch cleanup failures at `src/runtime/runtime.cpp:222-240` and `:558-572`; **confirmed** captured stream descriptors not closed on every task removal path at `src/task/runner.cpp:187-200` and `:356-362`; **confirmed** the current acceptance snapshot is stale and the original hard gates remain unverified.

**Verdict**: NEEDS_CHANGES

## High-level view

Pipeline status aggregation and the foreground stop path are repaired for completed and foreground-stopped jobs, but **confirmed** the builtin waiter only checks for `Stopped` before entering its blocking loop. A stop delivered by that loop is recorded without a terminal result, so the waiter can block indefinitely. The native launch path now closes pipeline pipes when `error_pipe` creation fails, but **confirmed** fork-failure unwind and parent descriptor-save failure still have unsafe ownership behavior.

Ordinary here-documents and tab-stripped `<<-` bodies now deliver the expected bytes, including a multi-line tab probe. **confirmed** The parser still collects each document immediately from the source position of its delimiter token; with two documents on one command, the second collection starts at the first body and consumes through the second delimiter.

Task groups now receive TERM followed by KILL after the 100 ms grace period, and a TERM-resistant shell/descendant probe returned status 124 in about 130 ms. **confirmed** The runner does not close captured read descriptors before erasing an active task when a descendant retains the pipe writer, so stream ownership is not complete even though timeout escalation is.

The verifier now parses CTest and PTY progress correctly, derives gate status from measured progress and registry thresholds, and requires an acceptance-record hash. **confirmed** Its current evidence is still stale, the generated private-file hash list is not required to cover every registry artifact, and the verifier does not compare manifest and acceptance source identities. Bounded local correctness is therefore distinct from the unverified original campaign.

<details>
<summary>Issues (10)</summary>

1. **Stopped-job wait hang** — **confirmed** `wait_job` checks `JobState::Stopped` only before `waitpid`; after `reap_one` changes the job to `Stopped`, the loop still waits for terminal results. Check the state immediately after reaping and return the stopped status.
2. **Multiple here-document binding** — **confirmed** each redirection is collected from its target token’s following newline, so `cat <<A <<B` gives the second redirection the first body as well. Parse all here-document headers first, then consume bodies in header order from the command’s body region.
3. **Captured stream closure** — **confirmed** normal direct-child completion drains captured descriptors and erases `Active` without closing descriptors that remain nonblocking with `EAGAIN` while a descendant holds the writer. Close both read ends on every active-task removal path, while retaining any output already collected.
4. **Partial pipeline launch unwind** — **confirmed** fork failure sends TERM to prior direct PIDs and waits without a bounded KILL escalation; a TERM-resistant process can make launch failure hang and descendants are not addressed as a group. Use the established process group and bounded escalation before blocking waits.
5. **Parent descriptor save failure** — **confirmed** a `dup` failure is marked `-2`, but entries after the failed save remain `-1` and restoration interprets them as originally closed, closing still-open standard descriptors. Abort restoration without mutating unsaved descriptors, or track the save phase separately from an originally-closed descriptor.
6. **Evidence source identity** — **confirmed** the verifier permits an acceptance source at `HEAD^` for an evidence-only commit but never checks that `MANIFEST.json` carries the same source identity as `ACCEPTANCE.json` or that evidence payloads agree with it. Require the manifest and acceptance identities to match and keep the evidence-only parent rule explicit and bounded.
7. **Private artifact hash completeness** — **confirmed** the generator hashes the known census/concurrency/heavy-stress files, but the verifier hashes only entries present in `private_files`; it does not require every registry `required_artifact` to be represented there. Derive required private hash coverage from the registry and reject an omitted entry.
8. **Documentation contract drift** — **confirmed** `docs/TASK_RUNNER.md:11` and `docs/BUILD_STATUS.md:27` still describe SIGKILL escalation as open even though `src/task/runner.cpp:334-342` implements it; `docs/ACCEPTANCE.md:22-24` says mutation checks are isolated and private census data is not hashed, which does not match `tools/acceptance_test.py:31-47` and `tools/acceptance.py:83-95`. Update the claims to distinguish implemented escalation from untested descendant behavior and to describe the actual evidence workflow.
9. **Regression coverage gap** — **confirmed** `tests/runtime_cases.py:57-64` covers only one ordinary heredoc and the basic pipeline cases, while `tests/task_cases.py:28-40` covers only a normal sleep timeout; no focused case covers stop-then-wait, multiple heredocs, descriptor-save exhaustion, fork partial failure, surviving stream writers, or forged provenance/hash/private-manifest records. Add deterministic probes for these contracts before calling the repairs stable.
10. **Original hard gates remain unverified** — **confirmed** the current `acceptance/ACCEPTANCE.json:3,117-186` and `acceptance/MANIFEST.json:3-14` are untracked stale artifacts from an older source commit, and `python3 tools/verify.py` rejects them for stale provenance, missing acceptance hash, missing `foundation-fast`, absent heavy-stress evidence, and unverified heavy/private gates. Regenerate evidence for this source state and run the required authored, differential, malformed, PTY, concurrency, stress, sanitizer, Linux, census, and hosted-tip campaigns; do not promote the bounded pass to full acceptance.

</details>

<details>
<summary>Details</summary>

### Job status and native launch ownership

The stage-indexed terminal-status repair is effective: completed pipelines aggregate by stage order, and `wait_foreground` exits when `reap_one` marks a job stopped. The remaining waiter defect is narrower and reproducible. In `src/runtime/runtime.cpp:375-387`, `wait_job` enters `waitpid(-1, ..., 0)` while the job is still marked Running; a delivered `SIGSTOP` is passed to `reap_one`, which sets `JobState::Stopped` without adding to `results`, and the loop immediately waits again. A one-second bounded probe of `sleep 37 & kill -STOP $!; wait %1` did not return until the shell was forcibly killed. **confirmed**

The error-pipe allocation repair is verified in source: `src/runtime/runtime.cpp:521-528` now closes every previously created pipeline descriptor before returning when `pipe(error_pipe)` fails. **confirmed** The other launch-failure path remains unsafe at `:558-572`: it signals individual prior PIDs with TERM and performs unbounded direct `waitpid` calls. The process-group setup already established for the pipeline should be reused for escalation, with descriptors closed after bounded reaping.

The parent descriptor repair correctly distinguishes an `EBADF` descriptor from a non-EBADF `dup` failure only for the slot that failed (`src/runtime/runtime.cpp:222-227`). **confirmed** `restore_parent_descriptors` still treats later untouched `-1` slots as originally closed (`:234-240`). With a four-descriptor limit, a parent builtin redirection probe returned 1 with no output, consistent with stdout/stderr being closed during failed restoration. The save operation must be all-or-nothing before any restoration logic assumes a slot was originally absent.

### Here-document body association

The new `<<-` normalization is effective: `src/syntax/parser.cpp:109-129` strips leading tabs from delimiter matching and from each body line, and the probe containing one- and two-tab prefixes produced `one\ntwo\n`. **confirmed** The same collection model is not safe for multiple documents. `collect_here_document` starts at the newline after the current target (`:99-105`), so when the second target appears on the original command line it scans the first document’s body as part of the second document. The probe for `cat <<A <<B` produced `one\nA\ntwo\n` instead of only `two\n`. `pending_here_end_` (`:74-76` and `:130`) skips already-tokenized body text after parsing, but it cannot repair the body start or establish header-order association.

### Task timeout and captured streams

The task runner now records a termination timestamp, sends TERM at timeout, and sends KILL to the task process group after 100 ms (`src/task/runner.cpp:334-342`). A manifest running `/bin/sh -c 'trap "" TERM; sleep 10'` with a 20 ms timeout returned a timed-out status of 124 in about 0.13 seconds, confirming that the previous TERM-only behavior is repaired. **confirmed**

**confirmed** Captured output is drained through `drain_fd` (`src/task/runner.cpp:187-200`), which closes a descriptor only after EOF or an error other than `EAGAIN`/`EINTR`. On ordinary direct-child completion, `:356-362` drains and then erases the `Active` object without closing descriptors that are still open because a background descendant inherited the writer. A probe with `sleep 2 & exit 0` returned promptly and allowed the next task to complete, but the code still leaks those read ends for the remainder of the runner process. The timeout path has the same ownership risk if a descendant escapes the task process group.

### Acceptance integrity and campaign boundary

The regex repair is confirmed: `tools/verify.py:20-29` measures the current CTest and PTY artifacts as 8 and 1 rather than zero. Gate verdicts are derived from the measured value, command status, and registry minimum at `tools/verify.py:93-109`; they are not trusted from the recorded `verified` field. The acceptance hash is now mandatory at `:86-90`, and `tools/acceptance.py:83-95` records the current source and hashes the known private artifacts before hashing the acceptance record.

The source check at `tools/verify.py:68-76` is stricter than the former freshness check for this workflow: an acceptance record may name HEAD, or the immediate parent only when the current commit contains evidence/review files and no source changes. **confirmed** A remaining integrity gap is that `manifest.source_commit` is not compared with `acceptance.source_commit`, and required registry artifacts are checked for existence at `:93-97` but not required to appear in `manifest.private_files`. A future or forged manifest can therefore omit a private artifact from the hash set while leaving the verifier unaware.

**confirmed** The current worktree evidence demonstrates the acceptance boundary rather than passing it. The record identifies the old source at `acceptance/ACCEPTANCE.json:3`, omits `foundation-fast`, records a 4,198-line private census against the 10,000 threshold (`:159-163`), records zero heavy-concurrency/heavy-stress progress (`:175-186`), and the manifest has no acceptance hash or private-file section (`acceptance/MANIFEST.json:3-14`). `python3 tools/verify.py` rejects this state. The fresh build and focused CTest run establish bounded local correctness only; they do not establish the 1,200 authored cases, 20,000 differential programs, 10,000 malformed programs, 300 PTY sessions, 500-by-30 concurrency campaign, 180,000 stress cycles, 2,000,000 sanitizer executions, Linux profile, 10,000-line census, or hosted-tip gate.

### Documentation and test scope

The task and build-status documents should say that group KILL escalation is implemented but not comprehensively evidenced, while the acceptance document should say that the mutation script edits and restores the worktree and that private hashes are included when generated. **confirmed** The focused suite’s passing 8/8 result is useful bounded evidence, but **confirmed** its current cases do not exercise the newly repaired or still-open paths listed above.

</details>

<details>
<summary>File map</summary>

- `src/runtime/runtime.cpp` — job waiting, parent descriptor scope, pipeline launch error cleanup, and process-group lifecycle.
- `src/syntax/parser.cpp`, `src/syntax/parser.hpp` — here-document body collection, tab stripping, and deferred token skipping.
- `src/task/runner.cpp` — task process groups, timeout TERM/KILL escalation, capture draining, and outcome removal.
- `tools/acceptance.py` — bounded evidence generation, source recording, and public/private manifest hashes.
- `tools/verify.py` — evidence measurement, threshold derivation, provenance checks, and hash verification.
- `docs/TASK_RUNNER.md`, `docs/BUILD_STATUS.md`, `docs/ACCEPTANCE.md` — implementation and evidence-contract claims.
- `acceptance/ACCEPTANCE.json`, `acceptance/MANIFEST.json` — current stale local evidence records; the verifier rejects them.

The full implementation comparison is the repository diff for the reviewed change; validation was `cmake --build build`, `ctest --test-dir build --output-on-failure`, the focused probes described above, and Python syntax validation for the evidence tools.

</details>
