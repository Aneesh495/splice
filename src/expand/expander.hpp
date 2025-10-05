#pragma once

#include "expand/state.hpp"
#include "syntax/ast.hpp"

#include <functional>
#include <string>
#include <vector>

namespace splice::expand {

enum class ExpansionContext { CommandWord, Assignment, Redirection, HereDocument, Prompt };

struct Field {
  std::string value;
  bool quoted{false};
};

struct ExpansionResult {
  std::vector<Field> fields;
  std::string error;
  bool ambiguous{false};

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

using CommandSubstitution = std::function<std::string(const std::string&, int&)>;

class Expander {
 public:
  explicit Expander(ShellState& state, CommandSubstitution substitution = {})
      : state_(state), substitution_(std::move(substitution)) {}

  [[nodiscard]] ExpansionResult word(const syntax::Word& word,
                                     ExpansionContext context = ExpansionContext::CommandWord) const;
  [[nodiscard]] ExpansionResult words(const std::vector<syntax::Word>& words,
                                      ExpansionContext context = ExpansionContext::CommandWord) const;
  [[nodiscard]] std::string parameter(std::string_view expression, bool& defined,
                                      std::string& error) const;

 private:
  [[nodiscard]] std::string expand_fragment(std::string_view fragment, bool quoted,
                                            bool& had_unquoted, bool& had_quoted,
                                            std::string& error) const;
  [[nodiscard]] std::vector<Field> split_fields(std::string value, bool had_unquoted,
                                                bool had_quoted) const;
  [[nodiscard]] std::vector<Field> pathname_expand(std::vector<Field> fields,
                                                    bool had_unquoted) const;
  [[nodiscard]] std::string trim_pattern(std::string value, std::string_view pattern,
                                         bool longest, bool prefix) const;

  ShellState& state_;
  CommandSubstitution substitution_;
};

}  // namespace splice::expand
