#pragma once

#include "source/source.hpp"
#include "syntax/ast.hpp"
#include "syntax/token.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace splice::syntax {

struct ParseResult {
  Program program;
  source::Diagnostics diagnostics;
  bool incomplete{false};
};

class Parser {
 public:
  Parser(const source::SourceBuffer& source, std::vector<Token> tokens)
      : source_(source), tokens_(std::move(tokens)) {}

  [[nodiscard]] ParseResult run();

 private:
  [[nodiscard]] const Token& current() const noexcept;
  [[nodiscard]] const Token& peek(std::size_t distance) const noexcept;
  bool accept(TokenKind kind);
  bool expect(TokenKind kind, const char* message, const char* repair);
  bool is_reserved(std::string_view word) const;
  bool accept_reserved(std::string_view word);
  bool expect_reserved(std::string_view word, const char* message, const char* repair);
  void skip_newlines();

  CommandPtr parse_list(bool stop_at_right_paren = false, bool stop_at_right_brace = false);
  CommandPtr parse_and_or(bool stop_at_right_paren, bool stop_at_right_brace);
  CommandPtr parse_pipeline(bool stop_at_right_paren, bool stop_at_right_brace);
  CommandPtr parse_command(bool stop_at_right_paren, bool stop_at_right_brace);
  CommandPtr parse_simple();
  CommandPtr parse_if();
  CommandPtr parse_for();
  CommandPtr parse_while(bool until);
  CommandPtr parse_case();
  CommandPtr parse_cond_expr();
  CommandPtr parse_arith_command();
  CommandPtr parse_time();
  bool parse_redirection(SimpleCommand& command);
  bool parse_redirection_list(std::vector<Redirection>& redirections);
  bool collect_here_document(Redirection& redirection);
  void error(source::Span span, std::string message, std::string repair = {});

  const source::SourceBuffer& source_;
  std::vector<Token> tokens_;
  std::size_t index_{0};
  source::Diagnostics diagnostics_;
  bool incomplete_{false};
  std::size_t pending_here_end_{0};
};

}  // namespace splice::syntax
