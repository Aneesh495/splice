# Grammar, tokens, and AST

The lexer in `src/syntax/lexer.cpp` emits a longest-match token stream. It retains the original byte span and a `Word` made of typed parts instead of flattening quotes. The parser in `src/syntax/parser.cpp` consumes that stream without evaluating a word.

The implemented core grammar is:

```text
program       := newline* list? newline* EOF
list          := and_or ((';\n*' | newline+ | '&') and_or)*
and_or        := pipeline (('&&' | '||') pipeline)*
pipeline      := ('!')? command (('|' | '|&') command)*
command       := simple | '(' list ')' | '{' list '}'
simple        := (word | io_number? redirection)+
redirection   := '<' word | '>' word | '>>' word | '<<' word | '<<-' word
               | '<&' word | '>&' word
```

This is the executable foundation, not a claim that all reserved-word productions are present. `if`, loops, functions, and `case` remain explicit parser work rather than being accepted as ordinary words. An unmatched quote, missing redirection operand, unexpected delimiter, and an operator without its following command are diagnostics with source ranges and repair hints. End-of-input after a quote or pipeline is marked incomplete for interactive continuation.

## AST identity

`CommandKind` distinguishes simple commands, pipelines, AND/OR chains, sequences, backgrounds, subshells, groups, and the reserved space for functions. A `Redirection` carries its target word and source span. Plan construction later preserves the source order of those redirections.

`splice --dump-tokens` and `splice --dump-ast` are syntax-only operations. They do not run command substitutions, open redirections, touch the filesystem for globbing, or mutate shell state.
