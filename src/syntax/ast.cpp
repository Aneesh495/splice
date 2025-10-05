#include "syntax/ast.hpp"

#include <sstream>

namespace splice::syntax {
namespace {

std::string json_escape(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 4);
  for (const char character : value) {
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += character; break;
    }
  }
  return result;
}

void append_word(std::ostringstream& out, const Word& word) {
  out << "{\"span\":[" << word.span.begin << ',' << word.span.end
      << "],\"spelling\":\"" << json_escape(word.spelling) << "\",\"parts\":[";
  for (std::size_t index = 0; index < word.parts.size(); ++index) {
    if (index != 0) out << ',';
    const auto& part = word.parts[index];
    out << "{\"kind\":\"" << word_part_name(part.kind) << "\",\"span\":["
        << part.span.begin << ',' << part.span.end << "],\"text\":\""
        << json_escape(part.text) << "\"}";
  }
  out << "]}";
}

void append_command(std::ostringstream& out, const CommandPtr& command) {
  out << "{\"kind\":\"" << command_kind_name(command->kind) << "\",\"span\":["
      << command->span.begin << ',' << command->span.end << ']';
  if (!command->name.empty()) {
    out << ",\"name\":\"" << json_escape(command->name) << '\"';
  }
  if (command->kind == CommandKind::Simple) {
    out << ",\"words\":[";
    for (std::size_t index = 0; index < command->simple.words.size(); ++index) {
      if (index != 0) out << ',';
      append_word(out, command->simple.words[index]);
    }
    out << "],\"redirections\":[";
    for (std::size_t index = 0; index < command->simple.redirections.size(); ++index) {
      if (index != 0) out << ',';
      const auto& redirection = command->simple.redirections[index];
      out << "{\"kind\":\"" << redirection_kind_name(redirection.kind)
          << "\",\"fd\":" << redirection.fd << ",\"span\":["
          << redirection.span.begin << ',' << redirection.span.end << "],\"target\":";
      append_word(out, redirection.target);
      out << '}';
    }
    out << ']';
  } else {
    out << ",\"negated\":" << (command->negated ? "true" : "false")
        << ",\"operators\":[";
    for (std::size_t index = 0; index < command->operators.size(); ++index) {
      if (index != 0) out << ',';
      out << '"' << json_escape(command->operators[index]) << '"';
    }
    out << "],\"children\":[";
    for (std::size_t index = 0; index < command->children.size(); ++index) {
      if (index != 0) out << ',';
      append_command(out, command->children[index]);
    }
    out << ']';
  }
  out << '}';
}

}  // namespace

const char* command_kind_name(CommandKind kind) noexcept {
  switch (kind) {
    case CommandKind::Simple: return "simple";
    case CommandKind::Pipeline: return "pipeline";
    case CommandKind::AndOr: return "and-or";
    case CommandKind::Sequence: return "sequence";
    case CommandKind::Background: return "background";
    case CommandKind::Subshell: return "subshell";
    case CommandKind::Group: return "group";
    case CommandKind::Function: return "function";
  }
  return "unknown";
}

const char* redirection_kind_name(RedirectionKind kind) noexcept {
  switch (kind) {
    case RedirectionKind::Input: return "input";
    case RedirectionKind::Output: return "output";
    case RedirectionKind::Append: return "append";
    case RedirectionKind::HereDocument: return "here-document";
    case RedirectionKind::HereDocumentStrip: return "here-document-strip";
    case RedirectionKind::DupInput: return "dup-input";
    case RedirectionKind::DupOutput: return "dup-output";
  }
  return "unknown";
}

std::string dump_tokens_json(const std::vector<Token>& tokens) {
  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (index != 0) out << ',';
    const auto& token = tokens[index];
    out << "{\"kind\":\"" << token_name(token.kind) << "\",\"span\":["
        << token.span.begin << ',' << token.span.end << "],\"text\":\""
        << json_escape(token.text) << '"';
    if (token.kind == TokenKind::Word) {
      out << ",\"word\":";
      append_word(out, token.word);
    }
    out << '}';
  }
  out << ']';
  return out.str();
}

std::string dump_ast_json(const Program& program) {
  std::ostringstream out;
  out << "{\"span\":[" << program.span.begin << ',' << program.span.end << "],\"commands\":[";
  for (std::size_t index = 0; index < program.commands.size(); ++index) {
    if (index != 0) out << ',';
    append_command(out, program.commands[index]);
  }
  out << "]}";
  return out.str();
}

}  // namespace splice::syntax
