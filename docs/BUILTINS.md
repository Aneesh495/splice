# Builtin contracts

Builtin dispatch is a typed C++ contract in `src/builtins/builtins.cpp`, not a command-string substitution. A single foreground stateful builtin runs in the parent after its descriptors are saved and restored. A pipeline stage or background builtin runs in a child and cannot mutate the parent state.

The current real implementations are `:`, `true`, `false`, `echo`, `printf` (`%s`, `%d`, `%i`, `%b`, and repeat semantics), `pwd`, `cd`, `export`, `unset`, `readonly`, `set` (`-e`, `-u`, `-C`, and `-o pipefail`), `shift`, `command`, `type`, `hash`, `umask`, `read`, `help`, `kill`, and `exit`. `jobs`, `wait`, and `bg` are reserved for the runtime job-table integration; `exec` rejects rather than silently pretending to replace the shell.

Parent state changes are visible to following commands:

```sh
./build/splice -c 'name=splice; cd /tmp; printf "%s %s\\n" "$name" "$PWD"'
```

The unsupported builtin surface remains listed in the feature registry. A recognized name is not marked complete until its argument, status, redirection, and parent/child cases are independently tested.
