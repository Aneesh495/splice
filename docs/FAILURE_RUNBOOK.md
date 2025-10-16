# Failure and resource runbook

Before a heavy campaign record OS, kernel, architecture, compiler, CMake/Ninja, shell versions, locale, process limits, descriptor limits, and terminal availability. Do not lower a requested target when a machine is constrained. Mark the gate unverified and preserve the reason.

Common diagnostics:

- A missing `PROBE` marker is a failed benchmark sample, not an outlier to remove.
- A missing trace line invalidates any total derived from that trace.
- A child setup failure must appear on the close-on-exec error channel and must not leave an owned PID unreaped.
- PTY failures are reduced with readiness markers and exact session fixtures, never fixed with a larger sleep.
- A persistent zombie requires a process observation taken while the parent remains alive; a later `ps` after shell exit is not evidence.
- Differential disagreements are classified as match, reference difference, unspecified, unsupported, or harness failure. They are not silently selected in Splice's favor.

Tests use temporary directories and exact process IDs/groups. They do not sweep by executable name, mutate the user's login shell, or read the user's real history by default.
