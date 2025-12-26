#pragma once

#include "syntax/ast.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace splice::syntax {

struct Diagnostic {
  enum class Severity {
    Info,
    Warning,
    Error
  };

  Severity severity{Severity::Warning};
  std::string code;
  std::string message;
  std::string file;
  std::size_t line{0};
  std::size_t column{0};
  std::string suggestion;

  [[nodiscard]] std::string format() const;
  [[nodiscard]] std::string to_json() const;
};

struct FormatOptions {
  std::size_t indent_width{2};
  bool use_spaces{true};
  bool insert_final_newline{true};
};

class Formatter {
 public:
  explicit Formatter(FormatOptions options = {}) : options_(options) {}

  [[nodiscard]] std::string format(const Program& program) const;
  [[nodiscard]] std::string format_command(const CommandPtr& command, std::size_t depth = 0) const;

 private:
  void format_simple(const SimpleCommand& cmd, std::string& out) const;
  void format_redirection(const Redirection& redir, std::string& out) const;
  void format_if(const IfCommand& if_cmd, std::size_t depth, std::string& out) const;
  void format_for(const ForCommand& for_cmd, std::size_t depth, std::string& out) const;
  void format_while(const WhileCommand& while_cmd, std::size_t depth, std::string& out) const;
  void format_case(const CaseCommand& case_cmd, std::size_t depth, std::string& out) const;
  void format_cond(const CondNodePtr& node, std::string& out) const;
  void format_word(const Word& word, std::string& out) const;
  std::string indent(std::size_t depth) const;

  FormatOptions options_;
};

class Linter {
 public:
  Linter() = default;

  [[nodiscard]] std::vector<Diagnostic> lint(const Program& program,
                                             const source::SourceBuffer* source = nullptr) const;

 private:
  void check_command(const CommandPtr& command, const source::SourceBuffer* source,
                     std::vector<Diagnostic>& diagnostics) const;
  void check_unquoted_expansion(const Word& word, const source::SourceBuffer* source,
                                std::vector<Diagnostic>& diagnostics) const;
  void check_unreachable_code(const std::vector<CommandPtr>& sequence, const source::SourceBuffer* source,
                              std::vector<Diagnostic>& diagnostics) const;
  void check_useless_cat(const CommandPtr& pipeline, const source::SourceBuffer* source,
                         std::vector<Diagnostic>& diagnostics) const;
};

}  // namespace splice::syntax
