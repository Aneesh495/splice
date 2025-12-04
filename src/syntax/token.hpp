#pragma once

#include "source/source.hpp"

#include <string>
#include <vector>

namespace splice::syntax {

enum class TokenKind {
  End,
  Word,
  IoNumber,
  Newline,
  Semicolon,
  Ampersand,
  Pipe,
  PipeAnd,
  AndIf,
  OrIf,
  Less,
  Greater,
  Append,
  HereDocument,
  HereDocumentStrip,
  DupInput,
  DupOutput,
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  Bang,
  HereString,
  AndGreater,
  AndAppend,
  Clobber,
  DLeftParen,
  DRightParen,
  DLeftBracket,
  DRightBracket,
  SemiSemi,
  SemiAnd,
  SemiSemiAnd,
};

enum class WordPartKind {
  Literal,
  SingleQuoted,
  DoubleQuoted,
  Escaped,
  Parameter,
  Arithmetic,
  CommandSubstitution,
};

struct WordPart {
  WordPartKind kind{WordPartKind::Literal};
  source::Span span{};
  std::string text;
};

struct Word {
  source::Span span{};
  std::string spelling;
  std::vector<WordPart> parts;

  [[nodiscard]] bool empty() const noexcept { return parts.empty() && spelling.empty(); }
};

struct Token {
  TokenKind kind{TokenKind::End};
  source::Span span{};
  std::string text;
  Word word{};

  [[nodiscard]] bool is(TokenKind expected) const noexcept { return kind == expected; }
};

[[nodiscard]] const char* token_name(TokenKind kind) noexcept;
[[nodiscard]] const char* word_part_name(WordPartKind kind) noexcept;

}  // namespace splice::syntax
