#include "syntax/token.hpp"

namespace splice::syntax {

const char* token_name(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::End: return "end of input";
    case TokenKind::Word: return "word";
    case TokenKind::IoNumber: return "io number";
    case TokenKind::Newline: return "newline";
    case TokenKind::Semicolon: return "semicolon";
    case TokenKind::Ampersand: return "ampersand";
    case TokenKind::Pipe: return "pipe";
    case TokenKind::PipeAnd: return "pipe-and";
    case TokenKind::AndIf: return "and-if";
    case TokenKind::OrIf: return "or-if";
    case TokenKind::Less: return "less-than";
    case TokenKind::Greater: return "greater-than";
    case TokenKind::Append: return "append";
    case TokenKind::HereDocument: return "here-document";
    case TokenKind::HereDocumentStrip: return "here-document-strip-tabs";
    case TokenKind::DupInput: return "duplicate-input";
    case TokenKind::DupOutput: return "duplicate-output";
    case TokenKind::LeftParen: return "left-parenthesis";
    case TokenKind::RightParen: return "right-parenthesis";
    case TokenKind::LeftBrace: return "left-brace";
    case TokenKind::RightBrace: return "right-brace";
    case TokenKind::Bang: return "bang";
    case TokenKind::HereString: return "here-string";
    case TokenKind::AndGreater: return "and-greater";
    case TokenKind::AndAppend: return "and-append";
    case TokenKind::Clobber: return "clobber";
    case TokenKind::DLeftParen: return "double-left-parenthesis";
    case TokenKind::DRightParen: return "double-right-parenthesis";
    case TokenKind::DLeftBracket: return "double-left-bracket";
    case TokenKind::DRightBracket: return "double-right-bracket";
    case TokenKind::SemiSemi: return "semi-semi";
    case TokenKind::SemiAnd: return "semi-and";
    case TokenKind::SemiSemiAnd: return "semi-semi-and";
  }
  return "unknown";
}

const char* word_part_name(WordPartKind kind) noexcept {
  switch (kind) {
    case WordPartKind::Literal: return "literal";
    case WordPartKind::SingleQuoted: return "single-quoted";
    case WordPartKind::DoubleQuoted: return "double-quoted";
    case WordPartKind::Escaped: return "escaped";
    case WordPartKind::Parameter: return "parameter";
    case WordPartKind::Arithmetic: return "arithmetic";
    case WordPartKind::CommandSubstitution: return "command-substitution";
  }
  return "unknown";
}

}  // namespace splice::syntax
