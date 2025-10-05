#pragma once

#include "syntax/token.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace splice::syntax {

enum class RedirectionKind { Input, Output, Append, HereDocument, HereDocumentStrip, DupInput, DupOutput };

struct Redirection {
  int fd{-1};
  RedirectionKind kind{RedirectionKind::Input};
  Word target;
  source::Span span{};
  bool pipe_stderr{false};
};

struct SimpleCommand {
  source::Span span{};
  std::vector<Word> words;
  std::vector<Redirection> redirections;
};

enum class CommandKind { Simple, Pipeline, AndOr, Sequence, Background, Subshell, Group, Function };

struct Command;
using CommandPtr = std::shared_ptr<Command>;

struct Command {
  CommandKind kind{CommandKind::Simple};
  source::Span span{};
  SimpleCommand simple;
  std::vector<CommandPtr> children;
  std::vector<std::string> operators;
  std::string name;
  bool negated{false};
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
