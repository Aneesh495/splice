#include "syntax/parser.hpp"

#include <memory>
#include <string>

namespace splice::syntax {
namespace {

bool is_command_terminator(TokenKind kind) {
  return kind == TokenKind::End || kind == TokenKind::Newline ||
         kind == TokenKind::Semicolon || kind == TokenKind::Ampersand ||
         kind == TokenKind::Pipe || kind == TokenKind::PipeAnd ||
         kind == TokenKind::AndIf || kind == TokenKind::OrIf ||
         kind == TokenKind::RightParen || kind == TokenKind::RightBrace;
}

bool is_redirection(TokenKind kind) {
  return kind == TokenKind::Less || kind == TokenKind::Greater ||
         kind == TokenKind::Append || kind == TokenKind::HereDocument ||
         kind == TokenKind::HereDocumentStrip || kind == TokenKind::DupInput ||
         kind == TokenKind::DupOutput;
}

}  // namespace

const Token& Parser::current() const noexcept { return tokens_[index_]; }
const Token& Parser::peek(std::size_t distance) const noexcept {
  const std::size_t position = index_ + distance;
  return position < tokens_.size() ? tokens_[position] : tokens_.back();
}

bool Parser::accept(TokenKind kind) {
  if (!current().is(kind)) return false;
  ++index_;
  return true;
}

void Parser::error(source::Span span, std::string message, std::string repair) {
  diagnostics_.error(span, std::move(message), std::move(repair));
}

bool Parser::expect(TokenKind kind, const char* message, const char* repair) {
  if (accept(kind)) return true;
  if (current().is(TokenKind::End)) incomplete_ = true;
  error(current().span, message, repair);
  return false;
}

void Parser::skip_newlines() {
  while (accept(TokenKind::Newline)) {}
}

CommandPtr Parser::parse_simple() {
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::Simple;
  command->span.begin = current().span.begin;
  bool saw_word = false;
  while (!is_command_terminator(current().kind)) {
    if (current().is(TokenKind::Word)) {
      command->simple.words.push_back(current().word);
      command->span.end = current().span.end;
      saw_word = true;
      ++index_;
      continue;
    }
    if (current().is(TokenKind::IoNumber) || is_redirection(current().kind)) {
      if (!parse_redirection(command->simple)) return nullptr;
      command->span.end = command->simple.redirections.back().span.end;
      continue;
    }
    error(current().span, "unexpected token in command", "separate commands with a list operator");
    ++index_;
  }
  if (!saw_word && command->simple.redirections.empty()) {
    if (current().is(TokenKind::End) || current().is(TokenKind::Newline)) {
      incomplete_ = true;
      error(current().span, "incomplete command after operator", "continue with a command");
    } else {
      error(current().span, "expected a command or compound command", "provide a command name");
    }
    return nullptr;
  }
  return command;
}

bool Parser::parse_redirection(SimpleCommand& command) {
  const std::size_t start = current().span.begin;
  int fd = -1;
  if (current().is(TokenKind::IoNumber)) {
    try {
      fd = std::stoi(current().text);
    } catch (...) {
      error(current().span, "invalid file descriptor", "use a decimal descriptor");
      ++index_;
      return false;
    }
    ++index_;
  }
  const Token operation = current();
  RedirectionKind kind;
  switch (operation.kind) {
    case TokenKind::Less: kind = RedirectionKind::Input; if (fd < 0) fd = 0; break;
    case TokenKind::Greater: kind = RedirectionKind::Output; if (fd < 0) fd = 1; break;
    case TokenKind::Append: kind = RedirectionKind::Append; if (fd < 0) fd = 1; break;
    case TokenKind::HereDocument: kind = RedirectionKind::HereDocument; if (fd < 0) fd = 0; break;
    case TokenKind::HereDocumentStrip: kind = RedirectionKind::HereDocumentStrip; if (fd < 0) fd = 0; break;
    case TokenKind::DupInput: kind = RedirectionKind::DupInput; if (fd < 0) fd = 0; break;
    case TokenKind::DupOutput: kind = RedirectionKind::DupOutput; if (fd < 0) fd = 1; break;
    default:
      error(operation.span, "expected a redirection operator", "use `<`, `>`, or a supported redirection operator");
      return false;
  }
  ++index_;
  if (!current().is(TokenKind::Word)) {
    if (current().is(TokenKind::End) || current().is(TokenKind::Newline)) incomplete_ = true;
    error(current().span, "redirection is missing its operand", "put a filename or descriptor after the operator");
    return false;
  }
  Redirection redirection;
  redirection.fd = fd;
  redirection.kind = kind;
  redirection.target = current().word;
  redirection.span = source::Span{start, current().span.end};
  command.redirections.push_back(std::move(redirection));
  ++index_;
  return true;
}

CommandPtr Parser::parse_command(bool stop_at_right_paren, bool stop_at_right_brace) {
  if (current().is(TokenKind::LeftParen)) {
    auto command = std::make_shared<Command>();
    command->kind = CommandKind::Subshell;
    command->span.begin = current().span.begin;
    ++index_;
    skip_newlines();
    const auto body = parse_list(true, false);
    if (body != nullptr) command->children.push_back(body);
    if (!expect(TokenKind::RightParen, "subshell is missing `)`", "close the subshell")) return command;
    command->span.end = tokens_[index_ - 1].span.end;
    return command;
  }
  if (current().is(TokenKind::LeftBrace)) {
    auto command = std::make_shared<Command>();
    command->kind = CommandKind::Group;
    command->span.begin = current().span.begin;
    ++index_;
    skip_newlines();
    const auto body = parse_list(false, true);
    if (body != nullptr) command->children.push_back(body);
    if (!expect(TokenKind::RightBrace, "command group is missing `}`", "close the command group")) return command;
    command->span.end = tokens_[index_ - 1].span.end;
    return command;
  }
  if (current().is(TokenKind::Bang)) {
    ++index_;
    auto command = parse_pipeline(stop_at_right_paren, stop_at_right_brace);
    if (command != nullptr) command->negated = !command->negated;
    return command;
  }
  return parse_simple();
}

CommandPtr Parser::parse_pipeline(bool stop_at_right_paren, bool stop_at_right_brace) {
  auto first = parse_command(stop_at_right_paren, stop_at_right_brace);
  if (first == nullptr) return nullptr;
  if (!current().is(TokenKind::Pipe) && !current().is(TokenKind::PipeAnd)) return first;
  auto pipeline = std::make_shared<Command>();
  pipeline->kind = CommandKind::Pipeline;
  pipeline->span.begin = first->span.begin;
  pipeline->children.push_back(first);
  while (current().is(TokenKind::Pipe) || current().is(TokenKind::PipeAnd)) {
    pipeline->operators.emplace_back(current().text);
    ++index_;
    if (current().is(TokenKind::End) || current().is(TokenKind::Newline)) incomplete_ = true;
    auto next = parse_command(stop_at_right_paren, stop_at_right_brace);
    if (next == nullptr) return pipeline;
    pipeline->children.push_back(next);
    pipeline->span.end = next->span.end;
  }
  return pipeline;
}

CommandPtr Parser::parse_and_or(bool stop_at_right_paren, bool stop_at_right_brace) {
  auto first = parse_pipeline(stop_at_right_paren, stop_at_right_brace);
  if (first == nullptr) return nullptr;
  if (!current().is(TokenKind::AndIf) && !current().is(TokenKind::OrIf)) return first;
  auto chain = std::make_shared<Command>();
  chain->kind = CommandKind::AndOr;
  chain->span.begin = first->span.begin;
  chain->children.push_back(first);
  while (current().is(TokenKind::AndIf) || current().is(TokenKind::OrIf)) {
    chain->operators.emplace_back(current().text);
    ++index_;
    if (current().is(TokenKind::End) || current().is(TokenKind::Newline)) incomplete_ = true;
    auto next = parse_pipeline(stop_at_right_paren, stop_at_right_brace);
    if (next == nullptr) return chain;
    chain->children.push_back(next);
    chain->span.end = next->span.end;
  }
  return chain;
}

CommandPtr Parser::parse_list(bool stop_at_right_paren, bool stop_at_right_brace) {
  skip_newlines();
  auto first = parse_and_or(stop_at_right_paren, stop_at_right_brace);
  if (first == nullptr) return nullptr;
  std::vector<CommandPtr> commands;
  commands.push_back(first);
  std::vector<std::string> operators;
  while (current().is(TokenKind::Semicolon) || current().is(TokenKind::Newline) ||
         current().is(TokenKind::Ampersand)) {
    const Token separator = current();
    ++index_;
    operators.push_back(separator.text);
    skip_newlines();
    if ((stop_at_right_paren && current().is(TokenKind::RightParen)) ||
        (stop_at_right_brace && current().is(TokenKind::RightBrace)) ||
        current().is(TokenKind::End)) {
      if (separator.is(TokenKind::Ampersand)) {
        auto background = std::make_shared<Command>();
        background->kind = CommandKind::Background;
        background->span = first->span;
        background->children.push_back(commands.back());
        commands.back() = background;
      }
      break;
    }
    auto next = parse_and_or(stop_at_right_paren, stop_at_right_brace);
    if (next == nullptr) break;
    if (separator.is(TokenKind::Ampersand)) {
      auto background = std::make_shared<Command>();
      background->kind = CommandKind::Background;
      background->span = commands.back()->span;
      background->children.push_back(commands.back());
      commands.back() = background;
    }
    commands.push_back(next);
  }
  if (commands.size() == 1) return commands.front();
  auto sequence = std::make_shared<Command>();
  sequence->kind = CommandKind::Sequence;
  sequence->span.begin = commands.front()->span.begin;
  sequence->span.end = commands.back()->span.end;
  sequence->children = std::move(commands);
  sequence->operators = std::move(operators);
  return sequence;
}

ParseResult Parser::run() {
  ParseResult result;
  result.program.span.begin = 0;
  skip_newlines();
  while (!current().is(TokenKind::End)) {
    const auto command = parse_list(false, false);
    if (command != nullptr) result.program.commands.push_back(command);
    skip_newlines();
    if (current().is(TokenKind::RightParen) || current().is(TokenKind::RightBrace)) {
      error(current().span, "unexpected closing delimiter", "remove this delimiter or open its group");
      ++index_;
    }
    if (current().is(TokenKind::End)) break;
  }
  result.program.span.end = source_.text().size();
  result.diagnostics = std::move(diagnostics_);
  result.incomplete = incomplete_;
  return result;
}

}  // namespace splice::syntax
