#include "syntax/lexer.hpp"

#include <cctype>
#include <string_view>

namespace splice::syntax {
namespace {

bool is_operator_start(char value, char next = '\0') {
  if (value == '[' && next == '[') return true;
  if (value == ']' && next == ']') return true;
  return value == ';' || value == '&' || value == '|' || value == '<' ||
         value == '>' || value == '(' || value == ')' || value == '{' ||
         value == '}' || value == '!';
}

bool is_boundary(char value, char next = '\0') {
  return value == '\0' || value == ' ' || value == '\t' || value == '\r' ||
         value == '\n' || is_operator_start(value, next);
}

}  // namespace

char Lexer::peek(std::size_t distance) const noexcept {
  const std::size_t index = offset_ + distance;
  return index < input_.size() ? input_[index] : '\0';
}

char Lexer::take() noexcept {
  if (at_end()) return '\0';
  return input_[offset_++];
}

void Lexer::error(source::Diagnostics& diagnostics, source::Span span,
                  std::string message, std::string repair) {
  diagnostics.error(span, std::move(message), std::move(repair));
}

void Lexer::skip_space_and_comments() {
  while (!at_end()) {
    if (peek() == ' ' || peek() == '\t' || peek() == '\r') {
      take();
      continue;
    }
    if (peek() == '#') {
      while (!at_end() && peek() != '\n') take();
      continue;
    }
    break;
  }
}

bool Lexer::scan_balanced(std::size_t open_start, std::size_t content_start,
                          std::string_view closing, std::string& content,
                          source::Diagnostics& diagnostics) {
  std::size_t depth = 1;
  std::size_t cursor = content_start;
  bool single = false;
  bool doubled = false;
  while (cursor < input_.size()) {
    const char value = input_[cursor];
    if (single) {
      if (value == '\'') single = false;
      ++cursor;
      continue;
    }
    if (doubled) {
      if (value == '\\') {
        cursor += cursor + 1 < input_.size() ? 2 : 1;
        continue;
      }
      if (value == '"') doubled = false;
      ++cursor;
      continue;
    }
    if (value == '\'') {
      single = true;
      ++cursor;
      continue;
    }
    if (value == '"') {
      doubled = true;
      ++cursor;
      continue;
    }
    if (value == '(' && closing == ")") {
      ++depth;
      ++cursor;
      continue;
    }
    if (value == ')' && closing == ")") {
      --depth;
      if (depth == 0) {
        content.assign(input_.substr(content_start, cursor - content_start));
        offset_ = cursor + 1;
        return true;
      }
      ++cursor;
      continue;
    }
    if (closing == ")") {
      ++cursor;
      continue;
    }
    ++cursor;
  }
  error(diagnostics, source::Span{open_start, input_.size()},
        "unterminated command substitution", "close the substitution with `)`");
  return false;
}

bool Lexer::scan_command_substitution(Word& word, source::Diagnostics& diagnostics) {
  const std::size_t start = offset_;
  offset_ += 2;
  std::string content;
  if (!scan_balanced(start, offset_, ")", content, diagnostics)) return false;
  word.parts.push_back(WordPart{WordPartKind::CommandSubstitution,
                                source::Span{start, offset_}, std::move(content)});
  return true;
}

bool Lexer::scan_arithmetic(Word& word, source::Diagnostics& diagnostics) {
  const std::size_t start = offset_;
  offset_ += 3;
  std::size_t cursor = offset_;
  std::size_t depth = 1;
  while (cursor + 1 < input_.size()) {
    if (input_[cursor] == '(') {
      ++depth;
      ++cursor;
      continue;
    }
    if (input_[cursor] == ')' && input_[cursor + 1] == ')') {
      --depth;
      if (depth == 0) {
        word.parts.push_back(WordPart{WordPartKind::Arithmetic,
                                      source::Span{start, cursor + 2},
                                      input_.substr(offset_, cursor - offset_)});
        offset_ = cursor + 2;
        return true;
      }
    }
    ++cursor;
  }
  error(diagnostics, source::Span{start, input_.size()},
        "unterminated arithmetic expansion", "close the expansion with `))`");
  return false;
}

bool Lexer::scan_parameter(Word& word, source::Diagnostics& diagnostics) {
  const std::size_t start = offset_;
  if (peek(1) == '(' && peek(2) == '(') return scan_arithmetic(word, diagnostics);
  if (peek(1) == '(') return scan_command_substitution(word, diagnostics);
  if (peek(1) == '{') {
    std::size_t cursor = offset_ + 2;
    while (cursor < input_.size() && input_[cursor] != '}') ++cursor;
    if (cursor == input_.size()) {
      error(diagnostics, source::Span{start, input_.size()},
            "unterminated parameter expansion", "close the expansion with `}`");
      return false;
    }
    word.parts.push_back(WordPart{WordPartKind::Parameter,
                                  source::Span{start, cursor + 1},
                                  input_.substr(offset_ + 2, cursor - offset_ - 2)});
    offset_ = cursor + 1;
    return true;
  }
  if (std::isalpha(static_cast<unsigned char>(peek(1))) || peek(1) == '_' ||
      std::isdigit(static_cast<unsigned char>(peek(1))) ||
      std::string_view("?$#!@*-_").find(peek(1)) != std::string_view::npos) {
    const std::size_t end = offset_ + 2;
    word.parts.push_back(WordPart{WordPartKind::Parameter,
                                  source::Span{start, end},
                                  input_.substr(offset_ + 1, 1)});
    offset_ = end;
    return true;
  }
  return false;
}

bool Lexer::scan_word(LexResult& result) {
  const std::size_t start = offset_;
  Word word;
  word.span.begin = start;
  std::string literal;
  auto flush_literal = [&]() {
    if (!literal.empty()) {
      word.parts.push_back(WordPart{WordPartKind::Literal,
                                    source::Span{offset_ - literal.size(), offset_},
                                    std::move(literal)});
      literal.clear();
    }
  };

  while (!at_end()) {
    const char value = peek();
    if (is_boundary(value, peek(1))) break;
    if (value == '\\') {
      flush_literal();
      const std::size_t escape_start = offset_++;
      if (at_end()) {
        error(result.diagnostics, source::Span{escape_start, offset_},
              "backslash at end of input", "provide a character after the backslash");
        result.incomplete = true;
        return false;
      }
      if (peek() == '\n') {
        ++offset_;
        continue;
      }
      word.parts.push_back(WordPart{WordPartKind::Escaped,
                                    source::Span{escape_start, offset_ + 1},
                                    std::string(1, take())});
      continue;
    }
    if (value == '\'') {
      flush_literal();
      const std::size_t quote_start = offset_++;
      const std::size_t content_start = offset_;
      while (!at_end() && peek() != '\'') ++offset_;
      if (at_end()) {
        error(result.diagnostics, source::Span{quote_start, offset_},
              "unterminated single quote", "close the quote with `'`");
        result.incomplete = true;
        return false;
      }
      word.parts.push_back(WordPart{WordPartKind::SingleQuoted,
                                    source::Span{quote_start, offset_ + 1},
                                    input_.substr(content_start, offset_ - content_start)});
      ++offset_;
      continue;
    }
    if (value == '"') {
      flush_literal();
      const std::size_t quote_start = offset_++;
      const std::size_t content_start = offset_;
      while (!at_end()) {
        if (peek() == '\\' && peek(1) != '\0') {
          offset_ += 2;
          continue;
        }
        if (peek() == '"') break;
        ++offset_;
      }
      if (at_end()) {
        error(result.diagnostics, source::Span{quote_start, offset_},
              "unterminated double quote", "close the quote with `\"`");
        result.incomplete = true;
        return false;
      }
      word.parts.push_back(WordPart{WordPartKind::DoubleQuoted,
                                    source::Span{quote_start, offset_ + 1},
                                    input_.substr(content_start, offset_ - content_start)});
      ++offset_;
      continue;
    }
    if (value == '$') {
      flush_literal();
      const std::size_t before = offset_;
      if (scan_parameter(word, result.diagnostics)) continue;
      offset_ = before;
    }
    literal.push_back(take());
  }
  flush_literal();
  if (word.parts.empty()) return false;
  word.span.end = offset_;
  word.spelling = input_.substr(start, offset_ - start);
  result.tokens.push_back(Token{TokenKind::Word, word.span, word.spelling, std::move(word)});
  at_token_start_ = false;
  return true;
}

bool Lexer::scan_operator(LexResult& result) {
  const std::size_t start = offset_;
  const char first = peek();
  TokenKind kind = TokenKind::End;
  std::size_t width = 1;
  if (first == ';') {
    kind = TokenKind::Semicolon;
    if (peek(1) == ';') {
      if (peek(2) == '&') {
        kind = TokenKind::SemiSemiAnd;
        width = 3;
      } else {
        kind = TokenKind::SemiSemi;
        width = 2;
      }
    } else if (peek(1) == '&') {
      kind = TokenKind::SemiAnd;
      width = 2;
    }
  } else if (first == '&') {
    kind = TokenKind::Ampersand;
    if (peek(1) == '&') {
      kind = TokenKind::AndIf;
      width = 2;
    } else if (peek(1) == '>') {
      if (peek(2) == '>') {
        kind = TokenKind::AndAppend;
        width = 3;
      } else {
        kind = TokenKind::AndGreater;
        width = 2;
      }
    }
  } else if (first == '|') {
    kind = TokenKind::Pipe;
    if (peek(1) == '|') {
      kind = TokenKind::OrIf;
      width = 2;
    } else if (peek(1) == '&') {
      kind = TokenKind::PipeAnd;
      width = 2;
    }
  } else if (first == '<') {
    kind = TokenKind::Less;
    if (peek(1) == '<') {
      if (peek(2) == '<') {
        kind = TokenKind::HereString;
        width = 3;
      } else if (peek(2) == '-') {
        kind = TokenKind::HereDocumentStrip;
        width = 3;
      } else {
        kind = TokenKind::HereDocument;
        width = 2;
      }
    } else if (peek(1) == '&') {
      kind = TokenKind::DupInput;
      width = 2;
    }
  } else if (first == '>') {
    kind = TokenKind::Greater;
    if (peek(1) == '>') {
      kind = TokenKind::Append;
      width = 2;
    } else if (peek(1) == '&') {
      kind = TokenKind::DupOutput;
      width = 2;
    } else if (peek(1) == '|') {
      kind = TokenKind::Clobber;
      width = 2;
    }
  } else if (first == '(') {
    kind = TokenKind::LeftParen;
    if (peek(1) == '(') {
      kind = TokenKind::DLeftParen;
      width = 2;
    }
  } else if (first == ')') {
    kind = TokenKind::RightParen;
    if (peek(1) == ')') {
      kind = TokenKind::DRightParen;
      width = 2;
    }
  } else if (first == '{') kind = TokenKind::LeftBrace;
  else if (first == '}') kind = TokenKind::RightBrace;
  else if (first == '!') kind = TokenKind::Bang;
  else if (first == '[' && peek(1) == '[') {
    kind = TokenKind::DLeftBracket;
    width = 2;
  } else if (first == ']' && peek(1) == ']') {
    kind = TokenKind::DRightBracket;
    width = 2;
  } else return false;

  offset_ += width;
  result.tokens.push_back(Token{kind, source::Span{start, offset_},
                                input_.substr(start, width), {}});
  at_token_start_ = true;
  return true;
}

LexResult Lexer::run() {
  LexResult result;
  while (!at_end()) {
    skip_space_and_comments();
    if (at_end()) break;
    if (peek() == '\n') {
      const std::size_t start = offset_++;
      result.tokens.push_back(Token{TokenKind::Newline, source::Span{start, offset_}, "\\n", {}});
      at_token_start_ = true;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(peek())) &&
        (peek(1) == '<' || peek(1) == '>')) {
      const std::size_t start = offset_++;
      while (std::isdigit(static_cast<unsigned char>(peek()))) ++offset_;
      result.tokens.push_back(Token{TokenKind::IoNumber, source::Span{start, offset_},
                                    input_.substr(start, offset_ - start), {}});
      continue;
    }
    if (is_operator_start(peek(), peek(1)) && scan_operator(result)) continue;
    if (!scan_word(result)) {
      if (!result.diagnostics.has_error()) {
        error(result.diagnostics, source::Span{offset_, offset_ + 1},
              "unexpected character in shell input", "remove or quote this character");
      }
      ++offset_;
    }
  }
  result.tokens.push_back(Token{TokenKind::End,
                                source::Span{input_.size(), input_.size()}, "", {}});
  return result;
}

}  // namespace splice::syntax
