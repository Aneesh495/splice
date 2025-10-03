# Language and profile contract

Splice starts from a finite POSIX shell foundation and adds a documented Bash-compatible profile. It does not claim full Bash or certified POSIX conformance. The command-line profiles are `posix`, `bash`, and `splice`; the default will be selected by the application contract once execution is wired.

The feature registry is machine-readable at [`docs/feature_registry.json`](feature_registry.json). Each feature has an identifier, semantic scope, and individually authored cases as they land. A feature is not marked implemented merely because a parser accepts its spelling.

## Current contract

At the initial checkpoint only command-line parsing is implemented. Shell source is not executed yet. This explicit limitation prevents a syntax-only scaffold from being mistaken for a runtime.

## Planned foundation groups

F01 external lookup, F02 quoting and escapes, F03 lists, F04 pipelines, F05 input redirection, F06 output/noclobber, F07 append, F08 descriptor duplication, F09 here-documents, F10 asynchronous lists, F11 AND, F12 OR, F13 negation, F14 brace groups, F15 subshells, F16 assignments/export, F17 parameters, F18 parameter operators, F19 command substitution, F20 arithmetic, F21 field splitting, F22 pathname expansion, F23 tilde, F24 functions, F25 conditionals, F26 loops, F27 case, F28 stateful builtins, F29 jobs, and F30 signals/terminal handoff.

Support for a group will be documented only after its real-shell success, boundary, and rejection cases pass.
