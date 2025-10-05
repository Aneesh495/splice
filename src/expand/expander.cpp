#include "expand/expander.hpp"

#include <algorithm>
#include <cctype>
#include <fnmatch.h>
#include <glob.h>

namespace splice::expand {
namespace {

bool is_special_parameter(char value) {
  return value == '?' || value == '$' || value == '!' || value == '#' ||
         value == '@' || value == '*' || value == '-' ||
         std::isdigit(static_cast<unsigned char>(value));
}

bool has_pattern(std::string_view value) {
  bool escaped = false;
  for (char value_char : value) {
    if (escaped) {
      escaped = false;
    } else if (value_char == '\\') {
      escaped = true;
    } else if (value_char == '*' || value_char == '?' || value_char == '[') {
      return true;
    }
  }
  return false;
}

std::string join_values(const std::vector<std::string>& values, std::string_view separator) {
  std::string result;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) result += separator;
    result += values[index];
  }
  return result;
}

}  // namespace

std::string Expander::trim_pattern(std::string value, std::string_view pattern,
                                   bool longest, bool prefix) const {
  std::string result = value;
  if (prefix) {
    for (std::size_t length = 0; length <= value.size(); ++length) {
      const std::string candidate = value.substr(0, length);
      if (fnmatch(std::string(pattern).c_str(), candidate.c_str(), 0) == 0) {
        result = value.substr(length);
        if (!longest) break;
      }
    }
  } else {
    for (std::size_t length = 0; length <= value.size(); ++length) {
      const std::size_t start = value.size() - length;
      const std::string candidate = value.substr(start);
      if (fnmatch(std::string(pattern).c_str(), candidate.c_str(), 0) == 0) {
        result = value.substr(0, start);
        if (!longest) break;
      }
    }
  }
  return result;
}

std::string Expander::parameter(std::string_view expression, bool& defined,
                                std::string& error) const {
  defined = true;
  if (expression == "?") return std::to_string(state_.last_status);
  if (expression == "$") return std::to_string(state_.shell_pid);
  if (expression == "!") return std::to_string(state_.last_background_pid);
  if (expression == "#") return std::to_string(state_.positional.size());
  if (expression == "@" || expression == "*") {
    const std::string ifs = state_.value("IFS");
    return join_values(state_.positional, ifs.empty() ? " " : ifs.substr(0, 1));
  }
  if (!expression.empty() && std::isdigit(static_cast<unsigned char>(expression.front()))) {
    try {
      const std::size_t number = std::stoul(std::string(expression));
      if (number == 0) return state_.value("0");
      if (number <= state_.positional.size()) return state_.positional[number - 1];
      defined = false;
      return {};
    } catch (...) {
      error = "invalid positional parameter";
      return {};
    }
  }

  std::string_view name = expression;
  std::string_view operation;
  const std::string_view operators[] = {":-", "-", ":=", "=", ":?", "?", ":+", "+", "##", "#", "%%", "%"};
  for (const auto candidate : operators) {
    const std::size_t position = expression.find(candidate);
    if (position != std::string_view::npos) {
      name = expression.substr(0, position);
      operation = expression.substr(position);
      break;
    }
  }
  if (!valid_name(name)) {
    error = "malformed parameter expansion name";
    return {};
  }
  const auto* variable = state_.find(name);
  defined = variable != nullptr;
  const std::string current = variable == nullptr ? std::string{} : variable->value;
  if (operation.empty()) {
    if (!defined && state_.option(ShellOption::Nounset)) error = "parameter is unset: " + std::string(name);
    return current;
  }
  const bool colon = operation.front() == ':';
  const char action = operation[colon ? 1 : 0];
  const std::string operand(operation.substr(colon ? 2 : 1));
  const bool use_operand = !defined || (colon && current.empty());
  switch (action) {
    case '-': return use_operand ? operand : current;
    case '=':
      if (!use_operand) return current;
      if (!state_.set(std::string(name), operand)) {
        error = "cannot assign readonly parameter: " + std::string(name);
        return {};
      }
      return operand;
    case '?':
      if (use_operand) {
        error = operand.empty() ? "parameter is required: " + std::string(name) : operand;
        return {};
      }
      return current;
    case '+': return use_operand ? std::string{} : operand;
    case '#': return trim_pattern(current, operand, operation.starts_with("##"), true);
    case '%': return trim_pattern(current, operand, operation.starts_with("%%"), false);
    default: error = "unsupported parameter operator"; return {};
  }
}

std::string Expander::expand_fragment(std::string_view fragment, bool quoted,
                                      bool& had_unquoted, bool& had_quoted,
                                      std::string& error) const {
  std::string result;
  for (std::size_t index = 0; index < fragment.size();) {
    const char value = fragment[index];
    if (value == '\\' && index + 1 < fragment.size()) {
      const char next = fragment[index + 1];
      if (!quoted || next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') {
        result.push_back(next);
        had_quoted = had_quoted || quoted;
        had_unquoted = had_unquoted || !quoted;
        index += 2;
        continue;
      }
    }
    if (value != '$') {
      result.push_back(value);
      had_quoted = had_quoted || quoted;
      had_unquoted = had_unquoted || !quoted;
      ++index;
      continue;
    }
    if (index + 1 >= fragment.size()) {
      result.push_back('$');
      ++index;
      continue;
    }
    if (fragment[index + 1] == '(') {
      std::size_t cursor = index + 2;
      std::size_t depth = 1;
      while (cursor < fragment.size() && depth != 0) {
        if (fragment[cursor] == '(') ++depth;
        else if (fragment[cursor] == ')') --depth;
        ++cursor;
      }
      if (depth != 0) {
        error = "unterminated command substitution";
        return {};
      }
      int status = 0;
      const std::string body(fragment.substr(index + 2, cursor - index - 3));
      if (!substitution_) {
        error = "command substitution is unavailable in this context";
        return {};
      }
      result += substitution_(body, status);
      index = cursor;
      had_quoted = had_quoted || quoted;
      had_unquoted = had_unquoted || !quoted;
      continue;
    }
    if (fragment[index + 1] == '(' && index + 2 < fragment.size() && fragment[index + 2] == '(') {
      const std::size_t end = fragment.find("))", index + 3);
      if (end == std::string_view::npos) {
        error = "unterminated arithmetic expansion";
        return {};
      }
      try {
        result += std::to_string(std::stoll(std::string(fragment.substr(index + 3, end - index - 3))));
      } catch (...) {
        error = "invalid arithmetic expression";
        return {};
      }
      index = end + 2;
      had_quoted = had_quoted || quoted;
      had_unquoted = had_unquoted || !quoted;
      continue;
    }
    std::size_t end = index + 2;
    std::string expression;
    if (fragment[index + 1] == '{') {
      const std::size_t close = fragment.find('}', index + 2);
      if (close == std::string_view::npos) {
        error = "unterminated parameter expansion";
        return {};
      }
      expression = std::string(fragment.substr(index + 2, close - index - 2));
      end = close + 1;
    } else if (is_special_parameter(fragment[index + 1])) {
      expression.assign(1, fragment[index + 1]);
    } else if (std::isalpha(static_cast<unsigned char>(fragment[index + 1])) || fragment[index + 1] == '_') {
      end = index + 2;
      while (end < fragment.size() && (std::isalnum(static_cast<unsigned char>(fragment[end])) || fragment[end] == '_')) ++end;
      expression = std::string(fragment.substr(index + 1, end - index - 1));
    } else {
      result.push_back('$');
      ++index;
      continue;
    }
    bool defined = false;
    result += parameter(expression, defined, error);
    if (!error.empty()) return {};
    index = end;
    had_quoted = had_quoted || quoted;
    had_unquoted = had_unquoted || !quoted;
  }
  return result;
}

std::vector<Field> Expander::split_fields(std::string value, bool had_unquoted,
                                          bool had_quoted) const {
  if (!had_unquoted) return {Field{std::move(value), had_quoted}};
  const std::string ifs = state_.value("IFS");
  if (ifs.empty()) return {Field{std::move(value), had_quoted}};
  std::vector<Field> fields;
  std::string current;
  auto flush = [&]() {
    if (!current.empty() || !fields.empty()) fields.push_back(Field{std::move(current), had_quoted});
    current.clear();
  };
  for (const char value_char : value) {
    if (ifs.find(value_char) == std::string::npos) current.push_back(value_char);
    else flush();
  }
  flush();
  if (fields.empty() && had_quoted) fields.push_back(Field{"", true});
  return fields;
}

std::vector<Field> Expander::pathname_expand(std::vector<Field> fields, bool had_unquoted) const {
  if (!had_unquoted) return fields;
  std::vector<Field> result;
  for (const auto& field : fields) {
    if (!has_pattern(field.value)) {
      result.push_back(field);
      continue;
    }
    glob_t matches{};
    if (glob(field.value.c_str(), GLOB_NOSORT, nullptr, &matches) == 0) {
      std::vector<std::string> paths;
      for (std::size_t index = 0; index < matches.gl_pathc; ++index) paths.emplace_back(matches.gl_pathv[index]);
      std::sort(paths.begin(), paths.end());
      for (auto& path : paths) result.push_back(Field{std::move(path), false});
    } else {
      result.push_back(field);
    }
    globfree(&matches);
  }
  return result;
}

ExpansionResult Expander::word(const syntax::Word& word, ExpansionContext context) const {
  ExpansionResult result;
  std::string value;
  bool had_unquoted = false;
  bool had_quoted = false;
  for (const auto& part : word.parts) {
    if (part.kind == syntax::WordPartKind::SingleQuoted) {
      value += part.text;
      had_quoted = true;
    } else if (part.kind == syntax::WordPartKind::Literal || part.kind == syntax::WordPartKind::DoubleQuoted) {
      const bool quoted = part.kind == syntax::WordPartKind::DoubleQuoted;
      value += expand_fragment(part.text, quoted, had_unquoted, had_quoted, result.error);
    } else if (part.kind == syntax::WordPartKind::Escaped) {
      value += part.text;
      had_quoted = true;
    } else if (part.kind == syntax::WordPartKind::Parameter) {
      bool defined = false;
      value += parameter(part.text, defined, result.error);
      had_unquoted = true;
    } else if (part.kind == syntax::WordPartKind::Arithmetic || part.kind == syntax::WordPartKind::CommandSubstitution) {
      const std::string wrapper = part.kind == syntax::WordPartKind::Arithmetic ? "$((" + part.text + "))" : "$(" + part.text + ")";
      value += expand_fragment(wrapper, false, had_unquoted, had_quoted, result.error);
    }
    if (!result.error.empty()) return result;
  }
  if (context != ExpansionContext::Prompt && !had_quoted && (value == "~" || value.starts_with("~/"))) {
    value = state_.value("HOME") + value.substr(1);
  }
  result.fields = pathname_expand(split_fields(std::move(value), had_unquoted, had_quoted), had_unquoted);
  if (context == ExpansionContext::Redirection && result.fields.size() != 1) {
    result.ambiguous = true;
    result.error = "redirection expands to more than one field";
  }
  return result;
}

ExpansionResult Expander::words(const std::vector<syntax::Word>& words,
                                ExpansionContext context) const {
  ExpansionResult result;
  for (const auto& word_value : words) {
    auto expanded = word(word_value, context);
    if (!expanded.ok()) return expanded;
    result.fields.insert(result.fields.end(), expanded.fields.begin(), expanded.fields.end());
  }
  return result;
}

}  // namespace splice::expand
