# Expansion boundary

`expand::Expander` is intentionally separate from parsing. It receives a typed `syntax::Word` and a context: command word, assignment, redirection, here-document, or prompt. The current implementation supports literal/single/double/escaped segments, special and named parameters, default/assign/error/alternate operators, pattern trimming, arithmetic integer literals, command-substitution callbacks, field splitting, tilde expansion, and pathname expansion.

The state object distinguishes unset variables from empty variables and tracks exported/readonly values, positional parameters, profile, options, last status, shell PID, and last background PID. Prefix assignments are represented in the plan before they are applied to a parent builtin or copied into an external environment.

The expansion result retains a field vector. This is important for quoted empty arguments and for rejecting an ambiguous redirection after expansion. The current field splitter is intentionally conservative and is covered by focused cases; Bash-profile array and fully provenance-aware mixed quoted/unquoted splitting are later gates.

Command substitution is a callback from the expander to the runtime. The syntax-only commands do not provide that callback, so a syntax inspection cannot execute a substitution. The runtime will drain substitution output concurrently and attach its status to the single status owner.
