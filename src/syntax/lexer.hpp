#pragma once

#include "source/source.hpp"
#include "syntax/token.hpp"

#include <vector>

namespace splice::syntax {

struct LexResult {
  std::vector<Token> tokens;
  source::Diagnostics diagnostics;
  bool incomplete{false};
};

class Lexer {
 public:
  explicit Lexer(const source::SourceBuffer& source) : input_(source.text()) {}

  [[nodiscard]] LexResult run();

 private:
  [[nodiscard]] bool at_end() const noexcept { return offset_ >= input_.size(); }
  [[nodiscard]] char peek(std::size_t distance = 0) const noexcept;
  char take() noexcept;
  void skip_space_and_comments();
  bool scan_operator(LexResult& result);
  bool scan_word(LexResult& result);
  bool scan_parameter(Word& word, source::Diagnostics& diagnostics);
  bool scan_command_substitution(Word& word, source::Diagnostics& diagnostics);
  bool scan_arithmetic(Word& word, source::Diagnostics& diagnostics);
  bool scan_balanced(std::size_t open_start, std::size_t content_start,
                     std::string_view closing, std::string& content,
                     source::Diagnostics& diagnostics);
  void error(source::Diagnostics& diagnostics, source::Span span,
             std::string message, std::string repair = {});

  const std::string& input_;
  std::size_t offset_{0};
  bool at_token_start_{true};
};

}  // namespace splice::syntax
