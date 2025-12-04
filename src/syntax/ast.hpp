#pragma once

#include "syntax/token.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace splice::syntax {

enum class RedirectionKind {
  Input,
  Output,
  Append,
  HereDocument,
  HereDocumentStrip,
  DupInput,
  DupOutput,
  HereString,
  OutputClobber,
  OutputAndStderr,
  AppendAndStderr
};

struct Redirection {
  int fd{-1};
  RedirectionKind kind{RedirectionKind::Input};
  Word target;
  source::Span span{};
  bool pipe_stderr{false};
  std::string here_body;
  bool here_strip_tabs{false};
};

struct SimpleCommand {
  source::Span span{};
  std::vector<Word> words;
  std::vector<Redirection> redirections;
};

enum class CommandKind {
  Simple,
  Pipeline,
  AndOr,
  Sequence,
  Background,
  Subshell,
  Group,
  Function,
  If,
  For,
  While,
  Until,
  Case,
  CondExpr,
  ArithCommand,
  Time
};

struct Command;
using CommandPtr = std::shared_ptr<Command>;

struct IfClause {
  CommandPtr condition;
  CommandPtr body;
};

struct IfCommand {
  std::vector<IfClause> clauses;
  CommandPtr else_body;
};

struct ForCommand {
  std::string name;
  std::vector<Word> words;
  bool is_arithmetic{false};
  std::string arith_init;
  std::string arith_cond;
  std::string arith_step;
  CommandPtr body;
};

struct WhileCommand {
  bool until{false};
  CommandPtr condition;
  CommandPtr body;
};

struct CaseItem {
  std::vector<Word> patterns;
  CommandPtr body;
};

struct CaseCommand {
  Word word;
  std::vector<CaseItem> items;
};

enum class CondOp {
  Unary,
  Binary,
  Not,
  And,
  Or
};

struct CondNode;
using CondNodePtr = std::shared_ptr<CondNode>;

struct CondNode {
  CondOp op{CondOp::Unary};
  std::string op_text;
  Word left;
  Word right;
  CondNodePtr left_child;
  CondNodePtr right_child;
};

struct TimeCommand {
  bool posix_format{false};
  CommandPtr command;
};

struct Command {
  CommandKind kind{CommandKind::Simple};
  source::Span span{};
  SimpleCommand simple;
  std::vector<CommandPtr> children;
  std::vector<std::string> operators;
  std::string name;
  bool negated{false};
  std::vector<Redirection> redirections;
  std::shared_ptr<IfCommand> if_cmd;
  std::shared_ptr<ForCommand> for_cmd;
  std::shared_ptr<WhileCommand> while_cmd;
  std::shared_ptr<CaseCommand> case_cmd;
  std::shared_ptr<CondNode> cond_expr;
  std::shared_ptr<TimeCommand> time_cmd;
  std::string arith_expr;
};

struct Program {
  source::Span span{};
  std::vector<CommandPtr> commands;
};

[[nodiscard]] const char* command_kind_name(CommandKind kind) noexcept;
[[nodiscard]] const char* redirection_kind_name(RedirectionKind kind) noexcept;
[[nodiscard]] std::string dump_tokens_json(const std::vector<Token>& tokens);
[[nodiscard]] std::string dump_ast_json(const Program& program);

}  // namespace splice::syntax
