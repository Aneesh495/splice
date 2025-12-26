#include "syntax/parser.hpp"

#include <algorithm>
#include <functional>
#include <string>

namespace splice::syntax {
namespace {

bool is_command_terminator(TokenKind kind) {
  return kind == TokenKind::End || kind == TokenKind::Newline ||
         kind == TokenKind::Semicolon || kind == TokenKind::Ampersand ||
         kind == TokenKind::Pipe || kind == TokenKind::PipeAnd ||
         kind == TokenKind::AndIf || kind == TokenKind::OrIf ||
         kind == TokenKind::RightParen || kind == TokenKind::RightBrace ||
         kind == TokenKind::SemiSemi || kind == TokenKind::SemiAnd ||
         kind == TokenKind::SemiSemiAnd || kind == TokenKind::DRightParen ||
         kind == TokenKind::DRightBracket;
}

bool is_redirection(TokenKind kind) {
  return kind == TokenKind::Less || kind == TokenKind::Greater ||
         kind == TokenKind::Append || kind == TokenKind::HereDocument ||
         kind == TokenKind::HereDocumentStrip || kind == TokenKind::DupInput ||
         kind == TokenKind::DupOutput || kind == TokenKind::HereString ||
         kind == TokenKind::Clobber || kind == TokenKind::AndGreater ||
         kind == TokenKind::AndAppend;
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

bool Parser::is_reserved(std::string_view word) const {
  if (!current().is(TokenKind::Word)) return false;
  if (current().word.spelling != word) return false;
  return current().word.parts.size() == 1 && current().word.parts.front().kind == WordPartKind::Literal;
}

bool Parser::accept_reserved(std::string_view word) {
  if (!is_reserved(word)) return false;
  ++index_;
  return true;
}

bool Parser::expect_reserved(std::string_view word, const char* message, const char* repair) {
  if (accept_reserved(word)) return true;
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
    if (saw_word && command->simple.words.back().spelling.ends_with('=') && current().is(TokenKind::LeftParen)) {
      Word& assign_word = command->simple.words.back();
      assign_word.spelling += "(";
      ++index_;
      while (!current().is(TokenKind::RightParen) && !current().is(TokenKind::End)) {
        if (current().is(TokenKind::Word)) {
          if (!assign_word.spelling.ends_with('(')) assign_word.spelling += " ";
          assign_word.spelling += current().word.spelling;
          for (const auto& p : current().word.parts) assign_word.parts.push_back(p);
        }
        ++index_;
      }
      if (current().is(TokenKind::RightParen)) {
        assign_word.spelling += ")";
        assign_word.span.end = current().span.end;
        command->span.end = current().span.end;
        ++index_;
      }
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
  if (pending_here_end_ != 0) {
    while (index_ < tokens_.size() && tokens_[index_].span.begin < pending_here_end_) ++index_;
    pending_here_end_ = 0;
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

bool Parser::collect_here_document(Redirection& redirection) {
  std::string delimiter = redirection.target.spelling;
  if (delimiter.size() >= 2 && ((delimiter.front() == '\'' && delimiter.back() == '\'') ||
                                (delimiter.front() == '"' && delimiter.back() == '"'))) {
    delimiter = delimiter.substr(1, delimiter.size() - 2);
  }
  const std::string& text = source_.text();
  std::size_t body_start = pending_here_end_ != 0 ? pending_here_end_ : text.find('\n', redirection.target.span.end);
  if (body_start == std::string::npos) {
    incomplete_ = true;
    error(redirection.target.span, "here-document is missing its body", "add a newline and a delimiter-terminated body");
    return false;
  }
  if (pending_here_end_ == 0) ++body_start;
  std::size_t cursor = body_start;
  while (cursor <= text.size()) {
    const std::size_t line_end = text.find('\n', cursor);
    const std::size_t end = line_end == std::string::npos ? text.size() : line_end;
    std::string line = text.substr(cursor, end - cursor);
    if (redirection.kind == RedirectionKind::HereDocumentStrip) {
      while (!line.empty() && line.front() == '\t') line.erase(line.begin());
    }
    if (line == delimiter) {
      redirection.here_body = text.substr(body_start, cursor - body_start);
      redirection.here_strip_tabs = redirection.kind == RedirectionKind::HereDocumentStrip;
      if (redirection.here_strip_tabs) {
        std::string normalized;
        std::size_t line_start = 0;
        while (line_start < redirection.here_body.size()) {
          const std::size_t line_end_body = redirection.here_body.find('\n', line_start);
          const std::size_t end_body = line_end_body == std::string::npos ? redirection.here_body.size() : line_end_body;
          std::size_t content_start = line_start;
          while (content_start < end_body && redirection.here_body[content_start] == '\t') ++content_start;
          normalized += redirection.here_body.substr(content_start, end_body - content_start);
          if (line_end_body != std::string::npos) normalized.push_back('\n');
          line_start = line_end_body == std::string::npos ? redirection.here_body.size() : line_end_body + 1;
        }
        redirection.here_body = std::move(normalized);
      }
      const std::size_t delimiter_end = line_end == std::string::npos ? end : line_end + 1;
      pending_here_end_ = std::max(pending_here_end_, delimiter_end);
      return true;
    }
    if (line_end == std::string::npos) break;
    cursor = line_end + 1;
  }
  incomplete_ = true;
  error(redirection.target.span, "here-document delimiter was not found", "add the delimiter on a line by itself");
  return false;
}

bool Parser::parse_redirection(SimpleCommand& command) {
  return parse_redirection_list(command.redirections);
}

bool Parser::parse_redirection_list(std::vector<Redirection>& redirections) {
  while (current().is(TokenKind::IoNumber) || is_redirection(current().kind)) {
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
      case TokenKind::HereString: kind = RedirectionKind::HereString; if (fd < 0) fd = 0; break;
      case TokenKind::Clobber: kind = RedirectionKind::OutputClobber; if (fd < 0) fd = 1; break;
      case TokenKind::AndGreater: kind = RedirectionKind::OutputAndStderr; if (fd < 0) fd = 1; break;
      case TokenKind::AndAppend: kind = RedirectionKind::AppendAndStderr; if (fd < 0) fd = 1; break;
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
    ++index_;
    if (kind == RedirectionKind::HereDocument || kind == RedirectionKind::HereDocumentStrip) {
      if (!collect_here_document(redirection)) return false;
    }
    redirections.push_back(std::move(redirection));
  }
  return true;
}

CommandPtr Parser::parse_if() {
  const std::size_t start = current().span.begin;
  ++index_; // skip 'if'
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::If;
  command->if_cmd = std::make_shared<IfCommand>();
  command->span.begin = start;

  skip_newlines();
  auto condition = parse_list(false, false);
  if (condition == nullptr) return nullptr;
  skip_newlines();
  if (!expect_reserved("then", "expected `then` after if condition", "add `then`")) return nullptr;
  skip_newlines();
  auto body = parse_list(false, false);
  command->if_cmd->clauses.push_back(IfClause{std::move(condition), std::move(body)});

  while (is_reserved("elif")) {
    ++index_;
    skip_newlines();
    auto elif_cond = parse_list(false, false);
    if (elif_cond == nullptr) return nullptr;
    skip_newlines();
    if (!expect_reserved("then", "expected `then` after elif condition", "add `then`")) return nullptr;
    skip_newlines();
    auto elif_body = parse_list(false, false);
    command->if_cmd->clauses.push_back(IfClause{std::move(elif_cond), std::move(elif_body)});
  }

  if (accept_reserved("else")) {
    skip_newlines();
    command->if_cmd->else_body = parse_list(false, false);
  }

  skip_newlines();
  if (!expect_reserved("fi", "expected `fi` to close if statement", "close with `fi`")) return nullptr;
  command->span.end = tokens_[index_ - 1].span.end;
  parse_redirection_list(command->redirections);
  return command;
}

CommandPtr Parser::parse_for() {
  const std::size_t start = current().span.begin;
  ++index_; // skip 'for'
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::For;
  command->for_cmd = std::make_shared<ForCommand>();
  command->span.begin = start;

  skip_newlines();
  if (current().is(TokenKind::DLeftParen)) {
    command->for_cmd->is_arithmetic = true;
    ++index_;
    std::string text;
    while (!current().is(TokenKind::DRightParen) && !current().is(TokenKind::End)) {
      text += current().text + " ";
      ++index_;
    }
    if (!expect(TokenKind::DRightParen, "expected `))` in arithmetic for", "close with `))`")) return nullptr;
    const std::size_t s1 = text.find(';');
    const std::size_t s2 = s1 != std::string::npos ? text.find(';', s1 + 1) : std::string::npos;
    if (s1 != std::string::npos) {
      command->for_cmd->arith_init = text.substr(0, s1);
      if (s2 != std::string::npos) {
        command->for_cmd->arith_cond = text.substr(s1 + 1, s2 - s1 - 1);
        command->for_cmd->arith_step = text.substr(s2 + 1);
      } else {
        command->for_cmd->arith_cond = text.substr(s1 + 1);
      }
    }
    if (current().is(TokenKind::Semicolon)) ++index_;
  } else {
    if (!current().is(TokenKind::Word)) {
      error(current().span, "expected variable name after for", "provide a variable name");
      return nullptr;
    }
    command->for_cmd->name = current().word.spelling;
    ++index_;
    skip_newlines();
    if (accept_reserved("in")) {
      while (!current().is(TokenKind::End) && !current().is(TokenKind::Newline) &&
             !current().is(TokenKind::Semicolon) && !is_reserved("do")) {
        if (current().is(TokenKind::Word)) {
          command->for_cmd->words.push_back(current().word);
          ++index_;
        } else {
          break;
        }
      }
      if (current().is(TokenKind::Semicolon) || current().is(TokenKind::Newline)) ++index_;
    } else if (current().is(TokenKind::Semicolon)) {
      ++index_;
    }
  }
  skip_newlines();
  if (!expect_reserved("do", "expected `do` after for header", "add `do`")) return nullptr;
  skip_newlines();
  command->for_cmd->body = parse_list(false, false);
  skip_newlines();
  if (!expect_reserved("done", "expected `done` to close for loop", "close with `done`")) return nullptr;
  command->span.end = tokens_[index_ - 1].span.end;
  parse_redirection_list(command->redirections);
  return command;
}

CommandPtr Parser::parse_while(bool until) {
  const std::size_t start = current().span.begin;
  ++index_; // skip 'while' or 'until'
  auto command = std::make_shared<Command>();
  command->kind = until ? CommandKind::Until : CommandKind::While;
  command->while_cmd = std::make_shared<WhileCommand>();
  command->while_cmd->until = until;
  command->span.begin = start;

  skip_newlines();
  command->while_cmd->condition = parse_list(false, false);
  if (command->while_cmd->condition == nullptr) return nullptr;
  skip_newlines();
  if (!expect_reserved("do", "expected `do` after loop condition", "add `do`")) return nullptr;
  skip_newlines();
  command->while_cmd->body = parse_list(false, false);
  skip_newlines();
  if (!expect_reserved("done", "expected `done` to close loop", "close with `done`")) return nullptr;
  command->span.end = tokens_[index_ - 1].span.end;
  parse_redirection_list(command->redirections);
  return command;
}

CommandPtr Parser::parse_case() {
  const std::size_t start = current().span.begin;
  ++index_; // skip 'case'
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::Case;
  command->case_cmd = std::make_shared<CaseCommand>();
  command->span.begin = start;

  skip_newlines();
  if (!current().is(TokenKind::Word)) {
    error(current().span, "expected word after case", "provide a word to match");
    return nullptr;
  }
  command->case_cmd->word = current().word;
  ++index_;
  skip_newlines();
  if (!expect_reserved("in", "expected `in` after case word", "add `in`")) return nullptr;
  skip_newlines();

  while (!is_reserved("esac") && !current().is(TokenKind::End)) {
    if (current().is(TokenKind::LeftParen)) ++index_;
    CaseItem item;
    while (!current().is(TokenKind::End)) {
      if (!current().is(TokenKind::Word)) {
        error(current().span, "expected pattern in case arm", "provide a pattern");
        return nullptr;
      }
      item.patterns.push_back(current().word);
      ++index_;
      if (current().is(TokenKind::Pipe)) {
        ++index_;
        continue;
      }
      if (current().is(TokenKind::RightParen)) {
        ++index_;
        break;
      }
    }
    skip_newlines();
    if (!is_reserved("esac") && !current().is(TokenKind::SemiSemi) &&
        !current().is(TokenKind::SemiAnd) && !current().is(TokenKind::SemiSemiAnd)) {
      item.body = parse_list(false, false);
    }
    skip_newlines();
    if (current().is(TokenKind::SemiSemi)) {
      item.terminator = ";;";
      ++index_;
      skip_newlines();
    } else if (current().is(TokenKind::SemiAnd)) {
      item.terminator = ";&";
      ++index_;
      skip_newlines();
    } else if (current().is(TokenKind::SemiSemiAnd)) {
      item.terminator = ";;&";
      ++index_;
      skip_newlines();
    }
    command->case_cmd->items.push_back(std::move(item));
  }

  if (!expect_reserved("esac", "expected `esac` to close case", "close with `esac`")) return nullptr;
  command->span.end = tokens_[index_ - 1].span.end;
  parse_redirection_list(command->redirections);
  return command;
}

CommandPtr Parser::parse_cond_expr() {
  const std::size_t start = current().span.begin;
  ++index_; // skip '[['
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::CondExpr;
  command->span.begin = start;

  std::vector<Token> cond_tokens;
  while (!current().is(TokenKind::DRightBracket) && !current().is(TokenKind::End)) {
    if (current().is(TokenKind::Newline)) {
      ++index_;
      continue;
    }
    cond_tokens.push_back(current());
    ++index_;
  }
  if (!expect(TokenKind::DRightBracket, "expected `]]` to close conditional expression", "close with `]]`")) return nullptr;
  command->span.end = tokens_[index_ - 1].span.end;

  if (!cond_tokens.empty()) {
    std::size_t c_idx = 0;
    std::function<CondNodePtr()> parse_expr_fn;
    std::function<CondNodePtr()> parse_and_fn;
    std::function<CondNodePtr()> parse_primary_fn;

    parse_primary_fn = [&]() -> CondNodePtr {
      if (c_idx >= cond_tokens.size()) return nullptr;
      if (cond_tokens[c_idx].is(TokenKind::LeftParen)) {
        ++c_idx;
        auto node = parse_expr_fn();
        if (c_idx < cond_tokens.size() && cond_tokens[c_idx].is(TokenKind::RightParen)) ++c_idx;
        return node;
      }
      if (cond_tokens[c_idx].is(TokenKind::Bang)) {
        ++c_idx;
        auto child = parse_primary_fn();
        auto node = std::make_shared<CondNode>();
        node->op = CondOp::Not;
        node->left_child = std::move(child);
        return node;
      }
      if (c_idx < cond_tokens.size()) {
        const auto& t1 = cond_tokens[c_idx++];
        if (c_idx < cond_tokens.size()) {
          const auto& t2 = cond_tokens[c_idx];
          if (t1.text.starts_with("-") && t1.text.size() == 2 &&
              !cond_tokens[c_idx].is(TokenKind::AndIf) && !cond_tokens[c_idx].is(TokenKind::OrIf)) {
            auto node = std::make_shared<CondNode>();
            node->op = CondOp::Unary;
            node->op_text = t1.text;
            node->left = cond_tokens[c_idx++].word;
            return node;
          }
          if (t2.text == "==" || t2.text == "!=" || t2.text == "=~" ||
              t2.text == "<" || t2.text == ">" || t2.text == "=" ||
              t2.text == "-eq" || t2.text == "-ne" || t2.text == "-lt" ||
              t2.text == "-le" || t2.text == "-gt" || t2.text == "-ge" ||
              t2.text == "-nt" || t2.text == "-ot" || t2.text == "-ef") {
            ++c_idx;
            Word right_word;
            if (c_idx < cond_tokens.size()) right_word = cond_tokens[c_idx++].word;
            auto node = std::make_shared<CondNode>();
            node->op = CondOp::Binary;
            node->op_text = t2.text;
            node->left = t1.word;
            node->right = std::move(right_word);
            return node;
          }
        }
        auto node = std::make_shared<CondNode>();
        node->op = CondOp::Unary;
        node->op_text = "-n";
        node->left = t1.word;
        return node;
      }
      return nullptr;
    };

    parse_and_fn = [&]() -> CondNodePtr {
      auto left = parse_primary_fn();
      while (c_idx < cond_tokens.size() && cond_tokens[c_idx].is(TokenKind::AndIf)) {
        ++c_idx;
        auto right = parse_primary_fn();
        auto node = std::make_shared<CondNode>();
        node->op = CondOp::And;
        node->left_child = std::move(left);
        node->right_child = std::move(right);
        left = node;
      }
      return left;
    };

    parse_expr_fn = [&]() -> CondNodePtr {
      auto left = parse_and_fn();
      while (c_idx < cond_tokens.size() && cond_tokens[c_idx].is(TokenKind::OrIf)) {
        ++c_idx;
        auto right = parse_and_fn();
        auto node = std::make_shared<CondNode>();
        node->op = CondOp::Or;
        node->left_child = std::move(left);
        node->right_child = std::move(right);
        left = node;
      }
      return left;
    };

    command->cond_expr = parse_expr_fn();
  }

  parse_redirection_list(command->redirections);
  return command;
}

CommandPtr Parser::parse_arith_command() {
  const std::size_t start = current().span.begin;
  ++index_; // skip '(('
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::ArithCommand;
  command->span.begin = start;
  std::string expr;
  while (!current().is(TokenKind::DRightParen) && !current().is(TokenKind::End)) {
    expr += current().text + " ";
    ++index_;
  }
  if (!expect(TokenKind::DRightParen, "expected `))` to close arithmetic command", "close with `))`")) return nullptr;
  command->span.end = tokens_[index_ - 1].span.end;
  command->arith_expr = std::move(expr);
  parse_redirection_list(command->redirections);
  return command;
}

CommandPtr Parser::parse_time() {
  const std::size_t start = current().span.begin;
  ++index_; // skip 'time'
  bool posix_format = false;
  if (current().is(TokenKind::Word) && current().word.spelling == "-p") {
    posix_format = true;
    ++index_;
  }
  skip_newlines();
  auto pipeline = parse_pipeline(false, false);
  if (pipeline == nullptr) return nullptr;
  auto command = std::make_shared<Command>();
  command->kind = CommandKind::Time;
  command->span.begin = start;
  command->span.end = pipeline->span.end;
  command->time_cmd = std::make_shared<TimeCommand>();
  command->time_cmd->posix_format = posix_format;
  command->time_cmd->command = std::move(pipeline);
  return command;
}

CommandPtr Parser::parse_command(bool stop_at_right_paren, bool stop_at_right_brace) {
  if (is_reserved("if")) return parse_if();
  if (is_reserved("for")) return parse_for();
  if (is_reserved("while")) return parse_while(false);
  if (is_reserved("until")) return parse_while(true);
  if (is_reserved("case")) return parse_case();
  if (is_reserved("time")) return parse_time();
  if (is_reserved("function")) {
    const std::size_t start = current().span.begin;
    ++index_;
    skip_newlines();
    if (!current().is(TokenKind::Word)) {
      error(current().span, "expected function name", "provide a valid function name");
      return nullptr;
    }
    std::string name = current().word.spelling;
    ++index_;
    skip_newlines();
    if (current().is(TokenKind::LeftParen)) {
      ++index_;
      if (!expect(TokenKind::RightParen, "expected `)` after `(`", "close with `)`")) return nullptr;
    }
    skip_newlines();
    auto body = parse_command(stop_at_right_paren, stop_at_right_brace);
    if (body == nullptr) return nullptr;
    auto cmd = std::make_shared<Command>();
    cmd->kind = CommandKind::Function;
    cmd->span.begin = start;
    cmd->span.end = body->span.end;
    cmd->name = std::move(name);
    cmd->children.push_back(std::move(body));
    parse_redirection_list(cmd->redirections);
    return cmd;
  }
  if (current().is(TokenKind::Word) && peek(1).is(TokenKind::LeftParen) && peek(2).is(TokenKind::RightParen)) {
    const std::size_t start = current().span.begin;
    std::string name = current().word.spelling;
    index_ += 3;
    skip_newlines();
    auto body = parse_command(stop_at_right_paren, stop_at_right_brace);
    if (body == nullptr) return nullptr;
    auto cmd = std::make_shared<Command>();
    cmd->kind = CommandKind::Function;
    cmd->span.begin = start;
    cmd->span.end = body->span.end;
    cmd->name = std::move(name);
    cmd->children.push_back(std::move(body));
    parse_redirection_list(cmd->redirections);
    return cmd;
  }
  if (current().is(TokenKind::DLeftBracket)) return parse_cond_expr();
  if (current().is(TokenKind::DLeftParen)) return parse_arith_command();

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
    parse_redirection_list(command->redirections);
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
    parse_redirection_list(command->redirections);
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
  if (is_reserved("then") || is_reserved("elif") || is_reserved("else") ||
      is_reserved("fi") || is_reserved("do") || is_reserved("done") ||
      is_reserved("esac") || current().is(TokenKind::SemiSemi) ||
      current().is(TokenKind::SemiAnd) || current().is(TokenKind::SemiSemiAnd)) {
    return nullptr;
  }
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
        is_reserved("then") || is_reserved("elif") || is_reserved("else") ||
        is_reserved("fi") || is_reserved("do") || is_reserved("done") ||
        is_reserved("esac") || current().is(TokenKind::SemiSemi) ||
        current().is(TokenKind::SemiAnd) || current().is(TokenKind::SemiSemiAnd) ||
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
    if (command != nullptr) {
      result.program.commands.push_back(command);
    } else {
      if (current().is(TokenKind::End)) break;
      error(current().span, "unexpected token: " + current().text, "remove or replace this token");
      ++index_;
    }
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
