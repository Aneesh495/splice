# Language and profile contract

Splice implements a comprehensive POSIX shell foundation and adds a documented Bash-compatible extension profile. The supported command-line profiles are `posix`, `bash`, and `splice`.

The feature registry is machine-readable at [`docs/feature_registry.json`](feature_registry.json). Each feature has an identifier, semantic scope, and individually authored cases that pass against the runtime engine.

## Runtime contract

Splice provides full execution capabilities across lexical, parsing, expansion, descriptor planning, process orchestration, and builtin execution stages.

### Foundation groups (F01 - F30)

All 30 POSIX foundation feature groups are fully implemented and verified with automated test suites:

- **F01 Quoting and escapes**: Single quotes, double quotes, backslash escapes, and multiline escape continuations.
- **F02 Assignments and environment**: Command-scoped environment assignments and persistent variable assignments.
- **F03 External commands**: Full PATH lookup, execution via `execve`, exit code propagation, and error diagnostics.
- **F04 Pipelines**: Multi-stage pipelines with bidirectional pipe descriptors, pipeline status handling, and SIGPIPE isolation.
- **F05 Redirections**: Input (`<`), output (`>`), append (`>>`), descriptor duplication (`>&`, `<&`), and clobber (`>|`).
- **F06 Here-documents**: Unquoted and quoted here-documents, including tab-stripped here-documents (`<<-`).
- **F07 AND and OR lists**: Short-circuit execution (`&&` and `||`) with correct status propagation.
- **F08 Sequences and groups**: Sequential execution (`;`), background execution (`&`), and brace command grouping (`{ list; }`).
- **F09 Subshells**: Process-isolated subshell execution (`( list )`) with pristine parent state preservation.
- **F10 Parameter expansion**: Scalar expansion (`$v`, `${v}`), parameter length (`${#v}`), and special parameters (`$?`, `$!`, `$$`, `$#`, `$@`, `$*`).
- **F11 Default and alternate values**: `${v:-def}`, `${v:=def}`, `${v:+alt}`, and `${v:?err}` with unset versus null distinction.
- **F12 Pattern trimming**: Prefix trimming (`${v#pat}`, `${v##pat}`) and suffix trimming (`${v%pat}`, `${v%%pat}`).
- **F13 Command substitution**: Dollar-parenthesis (`$(cmd)`) command substitution with trailing newline stripping.
- **F14 Arithmetic expansion**: Arithmetic evaluation (`$((expr))`) supporting standard operators, precedence, parens, and ternary expressions.
- **F15 Pathname expansion**: File system globbing (`*`, `?`, `[...]`) with character classes and sorting.
- **F16 Case conditional**: Pattern-based branching (`case word in pat) list;; ... esac`) with multi-pattern alternatives.
- **F17 If conditionals**: Conditional execution (`if list; then list; elif list; then list; else list; fi`).
- **F18 For loops**: Word list iteration (`for var in words; do list; done`).
- **F19 While loops**: Condition-driven iteration (`while list; do list; done`) and until iteration (`until list; do list; done`).
- **F20 Shell functions**: Function definition, local scope management (`local`), positional parameters, and `return`.
- **F21 Test builtins**: POSIX test expressions (`test` and `[ ... ]`) supporting file, string, and integer comparisons.
- **F22 Output utilities**: Formatted printing (`printf`) and echo utility (`echo`) with escape sequence handling.
- **F23 Directory navigation**: Working directory management (`cd`, `pwd`) with symlink resolution and `OLDPWD` tracking.
- **F24 Variable attributes**: Environment export (`export`) and write protection (`readonly`).
- **F25 Flow control**: Loop interruption (`break [n]`) and iteration skip (`continue [n]`).
- **F26 Eval and source**: Dynamic code evaluation (`eval`) and script sourcing (`source`, `.`).
- **F27 Signal traps**: Asynchronous and lifecycle signal traps (`trap 'action' SIGNAL`), including `EXIT` trap execution.
- **F28 Shell options**: Option toggling via `set -o` / `set +o` including `errexit`, `nounset`, `noclobber`, and `pipefail`.
- **F29 Positional parsing**: Option parsing utility (`getopts`) with option argument handling and error diagnostics.
- **F30 File creation mask**: Permission mask management (`umask`) with octal and query modes.

### Language extensions (X01 - X16)

Splice provides 16 modern language extensions beyond standard POSIX:

- **X01 Combined error pipeline**: Combined stdout and stderr pipeline routing (`|&`).
- **X02 Here-strings**: Direct string input redirection (`<<< string`).
- **X03 Combined output redirection**: Simultaneous stdout and stderr file redirection (`&>` and `&>>`).
- **X04 Extended test expressions**: Conditional expressions (`[[ expr ]]`) with pattern matching (`==`) and regular expressions (`=~`).
- **X05 Arithmetic evaluation command**: Arithmetic statement evaluation (`(( expr ))`) with truth-value status.
- **X06 Extended globbing**: Extglob pattern matching (`@(pat)`, `!(pat)`, `?(pat)`, `*(pat)`, `+(pat)`).
- **X07 Brace expansion**: String list expansion (`{a,b,c}`) and numeric/character sequence ranges (`{1..10}`).
- **X08 Substring slicing**: Zero-indexed string slicing (`${v:offset:length}`).
- **X09 Pattern replacement**: In-memory substring substitution (`${v/pat/rep}` and `${v//pat/rep}`).
- **X10 Case modification**: Upper-case (`${v^^}`) and lower-case (`${v,,}`) transformations.
- **X11 Indexed arrays**: Array assignment (`arr=(a b c)`), indexed access (`${arr[i]}`), and element counting (`${#arr[@]}`).
- **X12 C-style for loops**: Three-expression arithmetic iteration (`for ((i=0; i<N; ++i)); do ...; done`).
- **X13 Pipeline status array**: Stage-by-stage exit code tracking via the `PIPESTATUS` array.
- **X14 Directory stack**: Directory stack manipulation with `pushd`, `popd`, and `dirs`.
- **X15 Extended read options**: Extended input parsing including character counts (`read -n`), silent input (`read -s`), and custom delimiters (`read -d`).
- **X16 Command aliases**: Interactive command substitution and expansion (`alias` and `unalias`).
