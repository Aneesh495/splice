#include "syntax/formatter.hpp"

#include <algorithm>
#include <sstream>

namespace splice::syntax {

std::string Diagnostic::format() const {
  std::string result;
  if (!file.empty()) {
    result += file + ":";
    if (line > 0) result += std::to_string(line) + ":" + std::to_string(column) + ": ";
  }
  switch (severity) {
    case Severity::Info: result += "info: "; break;
    case Severity::Warning: result += "warning: "; break;
    case Severity::Error: result += "error: "; break;
  }
  if (!code.empty()) result += "[" + code + "] ";
  result += message;
  if (!suggestion.empty()) result += " (suggestion: " + suggestion + ")";
  return result;
}

std::string Diagnostic::to_json() const {
  const char* sev_str = severity == Severity::Info ? "info" :
                        severity == Severity::Warning ? "warning" : "error";
  std::string json = "{\"file\":\"" + file + "\",\"line\":" + std::to_string(line) +
                     ",\"column\":" + std::to_string(column) + ",\"severity\":\"" + sev_str +
                     "\",\"code\":\"" + code + "\",\"message\":\"" + message + "\"";
  if (!suggestion.empty()) json += ",\"suggestion\":\"" + suggestion + "\"";
  json += "}";
  return json;
}

std::string Formatter::indent(std::size_t depth) const {
  if (!options_.use_spaces) return std::string(depth, '\t');
  return std::string(depth * options_.indent_width, ' ');
}

void Formatter::format_word(const Word& word, std::string& out) const {
  for (const auto& part : word.parts) {
    out += part.text;
  }
}

void Formatter::format_redirection(const Redirection& redir, std::string& out) const {
  if (redir.fd >= 0) out += std::to_string(redir.fd);
  switch (redir.kind) {
    case RedirectionKind::Input: out += "< "; break;
    case RedirectionKind::Output: out += "> "; break;
    case RedirectionKind::Append: out += ">> "; break;
    case RedirectionKind::OutputClobber: out += ">| "; break;
    case RedirectionKind::HereString: out += "<<< "; break;
    case RedirectionKind::OutputAndStderr: out += "&> "; break;
    case RedirectionKind::AppendAndStderr: out += "&>> "; break;
    case RedirectionKind::DupInput: out += "<&"; break;
    case RedirectionKind::DupOutput: out += ">&"; break;
    case RedirectionKind::HereDocument: out += "<<"; break;
    case RedirectionKind::HereDocumentStrip: out += "<<-"; break;
  }
  format_word(redir.target, out);
}

void Formatter::format_simple(const SimpleCommand& cmd, std::string& out) const {
  bool first = true;
  for (const auto& word : cmd.words) {
    if (!first) out += " ";
    format_word(word, out);
    first = false;
  }
  for (const auto& redir : cmd.redirections) {
    if (!out.empty() && out.back() != ' ') out += " ";
    format_redirection(redir, out);
  }
}

void Formatter::format_cond(const CondNodePtr& node, std::string& out) const {
  if (node == nullptr) return;
  if (node->op == CondOp::Not) {
    out += "! ";
    format_cond(node->left_child, out);
    return;
  }
  if (node->op == CondOp::And) {
    format_cond(node->left_child, out);
    out += " && ";
    format_cond(node->right_child, out);
    return;
  }
  if (node->op == CondOp::Or) {
    format_cond(node->left_child, out);
    out += " || ";
    format_cond(node->right_child, out);
    return;
  }
  if (node->op == CondOp::Unary) {
    out += node->op_text + " ";
    format_word(node->left, out);
    return;
  }
  if (node->op == CondOp::Binary) {
    format_word(node->left, out);
    out += " " + node->op_text + " ";
    format_word(node->right, out);
    return;
  }
}

void Formatter::format_if(const IfCommand& if_cmd, std::size_t depth, std::string& out) const {
  const std::string ind = indent(depth);
  const std::string body_ind = indent(depth + 1);

  for (std::size_t i = 0; i < if_cmd.clauses.size(); ++i) {
    if (i == 0) {
      out += ind + "if ";
    } else {
      out += "\n" + ind + "elif ";
    }
    out += format_command(if_cmd.clauses[i].condition, 0) + "; then\n";
    if (if_cmd.clauses[i].body) {
      out += format_command(if_cmd.clauses[i].body, depth + 1);
    }
  }

  if (if_cmd.else_body) {
    out += "\n" + ind + "else\n";
    out += format_command(if_cmd.else_body, depth + 1);
  }

  out += "\n" + ind + "fi";
}

void Formatter::format_for(const ForCommand& for_cmd, std::size_t depth, std::string& out) const {
  const std::string ind = indent(depth);
  if (for_cmd.is_arithmetic) {
    out += ind + "for ((" + for_cmd.arith_init + "; " + for_cmd.arith_cond + "; " +
           for_cmd.arith_step + ")); do\n";
  } else {
    out += ind + "for " + for_cmd.name;
    if (!for_cmd.words.empty()) {
      out += " in";
      for (const auto& w : for_cmd.words) {
        out += " ";
        format_word(w, out);
      }
    }
    out += "; do\n";
  }
  if (for_cmd.body) {
    out += format_command(for_cmd.body, depth + 1);
  }
  out += "\n" + ind + "done";
}

void Formatter::format_while(const WhileCommand& while_cmd, std::size_t depth, std::string& out) const {
  const std::string ind = indent(depth);
  out += ind + (while_cmd.until ? "until " : "while ");
  if (while_cmd.condition) {
    out += format_command(while_cmd.condition, 0);
  }
  out += "; do\n";
  if (while_cmd.body) {
    out += format_command(while_cmd.body, depth + 1);
  }
  out += "\n" + ind + "done";
}

void Formatter::format_case(const CaseCommand& case_cmd, std::size_t depth, std::string& out) const {
  const std::string ind = indent(depth);
  const std::string item_ind = indent(depth + 1);
  out += ind + "case ";
  format_word(case_cmd.word, out);
  out += " in\n";

  for (const auto& item : case_cmd.items) {
    out += item_ind;
    for (std::size_t i = 0; i < item.patterns.size(); ++i) {
      if (i > 0) out += " | ";
      format_word(item.patterns[i], out);
    }
    out += ")\n";
    if (item.body) {
      out += format_command(item.body, depth + 2) + "\n";
    }
    out += indent(depth + 2) + item.terminator + "\n";
  }

  out += ind + "esac";
}

std::string Formatter::format_command(const CommandPtr& command, std::size_t depth) const {
  if (command == nullptr) return {};
  std::string out;
  const std::string ind = indent(depth);

  if (command->negated) out += "! ";

  switch (command->kind) {
    case CommandKind::Simple:
      out += ind;
      format_simple(command->simple, out);
      break;

    case CommandKind::Pipeline: {
      out += ind;
      for (std::size_t i = 0; i < command->children.size(); ++i) {
        if (i > 0) {
          const bool pipe_err = (i - 1 < command->operators.size() && command->operators[i - 1] == "|&");
          out += pipe_err ? " |& " : " | ";
        }
        out += format_command(command->children[i], 0);
      }
      break;
    }

    case CommandKind::AndOr: {
      out += ind;
      for (std::size_t i = 0; i < command->children.size(); ++i) {
        if (i > 0) {
          const std::string op = i - 1 < command->operators.size() ? command->operators[i - 1] : "&&";
          out += " " + op + " ";
        }
        out += format_command(command->children[i], 0);
      }
      break;
    }

    case CommandKind::Sequence: {
      for (std::size_t i = 0; i < command->children.size(); ++i) {
        if (i > 0) out += "\n";
        out += format_command(command->children[i], depth);
      }
      break;
    }

    case CommandKind::Subshell:
      out += ind + "(\n";
      for (const auto& child : command->children) {
        out += format_command(child, depth + 1) + "\n";
      }
      out += ind + ")";
      break;

    case CommandKind::Group:
      out += ind + "{\n";
      for (const auto& child : command->children) {
        out += format_command(child, depth + 1) + "\n";
      }
      out += ind + "}";
      break;

    case CommandKind::Function:
      out += ind + command->name + "() ";
      if (!command->children.empty()) {
        out += format_command(command->children.front(), depth);
      }
      break;

    case CommandKind::If:
      if (command->if_cmd) format_if(*command->if_cmd, depth, out);
      break;

    case CommandKind::For:
      if (command->for_cmd) format_for(*command->for_cmd, depth, out);
      break;

    case CommandKind::While:
    case CommandKind::Until:
      if (command->while_cmd) format_while(*command->while_cmd, depth, out);
      break;

    case CommandKind::Case:
      if (command->case_cmd) format_case(*command->case_cmd, depth, out);
      break;

    case CommandKind::CondExpr:
      out += ind + "[[ ";
      format_cond(command->cond_expr, out);
      out += " ]]";
      break;

    case CommandKind::ArithCommand:
      out += ind + "(( " + command->arith_expr + " ))";
      break;

    case CommandKind::Time:
      out += ind + "time ";
      if (command->time_cmd && command->time_cmd->command) {
        out += format_command(command->time_cmd->command, 0);
      }
      break;

    case CommandKind::Background:
      out += format_command(command->children.empty() ? nullptr : command->children.front(), depth);
      out += " &";
      break;
  }

  for (const auto& redir : command->redirections) {
    if (!out.empty() && out.back() != ' ') out += " ";
    format_redirection(redir, out);
  }

  return out;
}

std::string Formatter::format(const Program& program) const {
  std::string result;
  for (std::size_t i = 0; i < program.commands.size(); ++i) {
    if (i > 0) result += "\n";
    result += format_command(program.commands[i], 0);
  }
  if (options_.insert_final_newline && !result.empty() && result.back() != '\n') {
    result += "\n";
  }
  return result;
}

std::vector<Diagnostic> Linter::lint(const Program& program, const source::SourceBuffer* source) const {
  std::vector<Diagnostic> diagnostics;
  check_unreachable_code(program.commands, source, diagnostics);
  for (const auto& command : program.commands) {
    check_command(command, source, diagnostics);
  }
  return diagnostics;
}

void Linter::check_unquoted_expansion(const Word& word, const source::SourceBuffer* source,
                                      std::vector<Diagnostic>& diagnostics) const {
  for (const auto& part : word.parts) {
    if (part.kind == WordPartKind::Parameter && part.text.starts_with("$")) {
      Diagnostic diag;
      diag.severity = Diagnostic::Severity::Warning;
      diag.code = "SPLICE-001";
      if (source != nullptr) {
        diag.file = source->name();
        const auto pos = source->position(part.span.begin);
        diag.line = pos.line;
        diag.column = pos.column;
      }
      diag.message = "Unquoted parameter expansion `" + part.text + "` may undergo word splitting and globbing";
      diag.suggestion = "\"" + part.text + "\"";
      diagnostics.push_back(std::move(diag));
    }
  }
}

void Linter::check_useless_cat(const CommandPtr& pipeline, const source::SourceBuffer* source,
                              std::vector<Diagnostic>& diagnostics) const {
  if (pipeline == nullptr || pipeline->kind != CommandKind::Pipeline) return;
  if (pipeline->children.empty()) return;
  const auto& first = pipeline->children.front();
  if (first && first->kind == CommandKind::Simple && !first->simple.words.empty()) {
    std::string first_cmd;
    for (const auto& p : first->simple.words.front().parts) first_cmd += p.text;
    if (first_cmd == "cat" && first->simple.words.size() == 2) {
      std::string arg;
      for (const auto& p : first->simple.words[1].parts) arg += p.text;
      if (!arg.starts_with("-")) {
        Diagnostic diag;
        diag.severity = Diagnostic::Severity::Info;
        diag.code = "SPLICE-003";
        if (source != nullptr) {
          diag.file = source->name();
          const auto pos = source->position(first->span.begin);
          diag.line = pos.line;
          diag.column = pos.column;
        }
        diag.message = "Useless use of cat in pipeline: `" + first_cmd + " " + arg + "`";
        diag.suggestion = "use `< " + arg + "` redirection instead";
        diagnostics.push_back(std::move(diag));
      }
    }
  }
}

void Linter::check_unreachable_code(const std::vector<CommandPtr>& sequence, const source::SourceBuffer* source,
                                    std::vector<Diagnostic>& diagnostics) const {
  bool terminated = false;
  for (const auto& cmd : sequence) {
    if (cmd == nullptr) continue;
    if (terminated) {
      Diagnostic diag;
      diag.severity = Diagnostic::Severity::Warning;
      diag.code = "SPLICE-002";
      if (source != nullptr) {
        diag.file = source->name();
        const auto pos = source->position(cmd->span.begin);
        diag.line = pos.line;
        diag.column = pos.column;
      }
      diag.message = "Unreachable command after exit/return/exec";
      diagnostics.push_back(std::move(diag));
      break;
    }
    if (cmd->kind == CommandKind::Simple && !cmd->simple.words.empty()) {
      std::string name;
      for (const auto& p : cmd->simple.words.front().parts) name += p.text;
      if (name == "exit" || name == "return" || name == "exec") {
        terminated = true;
      }
    }
  }
}

void Linter::check_command(const CommandPtr& command, const source::SourceBuffer* source,
                           std::vector<Diagnostic>& diagnostics) const {
  if (command == nullptr) return;

  if (command->kind == CommandKind::Simple) {
    if (!command->simple.words.empty()) {
      std::string first_cmd;
      for (const auto& p : command->simple.words.front().parts) first_cmd += p.text;
      if (first_cmd == "[") {
        for (std::size_t i = 1; i < command->simple.words.size(); ++i) {
          std::string arg;
          for (const auto& p : command->simple.words[i].parts) arg += p.text;
          if (arg == "-a" || arg == "-o" || arg == "==") {
            Diagnostic diag;
            diag.severity = Diagnostic::Severity::Warning;
            diag.code = "SPLICE-004";
            if (source != nullptr) {
              diag.file = source->name();
              const auto pos = source->position(command->span.begin);
              diag.line = pos.line;
              diag.column = pos.column;
            }
            diag.message = "Fragile operator `" + arg + "` in `[` test; consider `[[ ... ]]`";
            diag.suggestion = "[[ ... ]]";
            diagnostics.push_back(std::move(diag));
            break;
          }
        }
      }
      for (std::size_t i = 1; i < command->simple.words.size(); ++i) {
        check_unquoted_expansion(command->simple.words[i], source, diagnostics);
      }
    }
  } else if (command->kind == CommandKind::Pipeline) {
    check_useless_cat(command, source, diagnostics);
    for (const auto& child : command->children) check_command(child, source, diagnostics);
  } else if (command->kind == CommandKind::Sequence) {
    check_unreachable_code(command->children, source, diagnostics);
    for (const auto& child : command->children) check_command(child, source, diagnostics);
  } else if (command->kind == CommandKind::If) {
    if (command->if_cmd) {
      for (const auto& clause : command->if_cmd->clauses) {
        if (!clause.body || (clause.body->kind == CommandKind::Simple && clause.body->simple.words.empty())) {
          Diagnostic diag;
          diag.severity = Diagnostic::Severity::Warning;
          diag.code = "SPLICE-005";
          if (source != nullptr) {
            diag.file = source->name();
            const auto pos = source->position(command->span.begin);
            diag.line = pos.line;
            diag.column = pos.column;
          }
          diag.message = "Empty branch in if-statement";
          diagnostics.push_back(std::move(diag));
        }
        check_command(clause.condition, source, diagnostics);
        check_command(clause.body, source, diagnostics);
      }
      if (command->if_cmd->else_body) check_command(command->if_cmd->else_body, source, diagnostics);
    }
  } else {
    for (const auto& child : command->children) check_command(child, source, diagnostics);
  }
}

}  // namespace splice::syntax
