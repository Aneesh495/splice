#include "expand/expander.hpp"

#include <algorithm>
#include <cctype>
#include <fnmatch.h>
#include <glob.h>
#include <limits>
#include <sstream>

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

class ArithmeticParser {
 public:
  explicit ArithmeticParser(std::string_view text, ShellState* state = nullptr)
      : text_(text), state_(state) {}

  bool run(long long& result) {
    skip();
    if (index_ >= text_.size()) {
      result = 0;
      return true;
    }
    result = parse_comma();
    skip();
    return !error_ && index_ == text_.size();
  }

 private:
  void skip() {
    while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_])) != 0) {
      ++index_;
    }
  }

  char peek() const {
    return index_ < text_.size() ? text_[index_] : '\0';
  }

  bool match(std::string_view prefix) {
    skip();
    if (text_.substr(index_).starts_with(prefix)) {
      index_ += prefix.size();
      return true;
    }
    return false;
  }

  long long parse_comma() {
    long long value = parse_assignment();
    while (match(",")) {
      value = parse_assignment();
    }
    return value;
  }

  long long parse_assignment() {
    skip();
    const std::size_t saved_index = index_;
    std::string var_name;
    bool has_subscript = false;
    long long subscript = 0;

    if (parse_variable_target(var_name, has_subscript, subscript)) {
      skip();
      std::string op;
      const std::string_view assign_ops[] = {
          "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=", "="};
      for (const auto& candidate : assign_ops) {
        if (text_.substr(index_).starts_with(candidate)) {
          op = candidate;
          index_ += candidate.size();
          break;
        }
      }
      if (!op.empty()) {
        const long long right = parse_assignment();
        long long current_val = 0;
        if (state_ != nullptr) {
          std::string str_val = has_subscript
                                    ? state_->get_array_element(var_name, static_cast<std::size_t>(subscript))
                                    : state_->value(var_name);
          if (!str_val.empty()) {
            ArithmeticParser(str_val, state_).run(current_val);
          }
        }
        long long new_val = right;
        if (op == "+=") new_val = current_val + right;
        else if (op == "-=") new_val = current_val - right;
        else if (op == "*=") new_val = current_val * right;
        else if (op == "/=") {
          if (right == 0) { error_ = true; return 0; }
          new_val = current_val / right;
        } else if (op == "%=") {
          if (right == 0) { error_ = true; return 0; }
          new_val = current_val % right;
        } else if (op == "<<=") new_val = current_val << right;
        else if (op == ">>=") new_val = current_val >> right;
        else if (op == "&=") new_val = current_val & right;
        else if (op == "^=") new_val = current_val ^ right;
        else if (op == "|=") new_val = current_val | right;

        if (state_ != nullptr) {
          if (has_subscript) {
            state_->set_array_element(var_name, static_cast<std::size_t>(subscript), std::to_string(new_val));
          } else {
            state_->set(var_name, std::to_string(new_val));
          }
        }
        return new_val;
      }
    }
    index_ = saved_index;
    return parse_conditional();
  }

  bool parse_variable_target(std::string& name, bool& has_subscript, long long& subscript) {
    skip();
    if (index_ >= text_.size() || (!std::isalpha(static_cast<unsigned char>(text_[index_])) && text_[index_] != '_')) {
      return false;
    }
    const std::size_t start = index_;
    while (index_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[index_])) || text_[index_] == '_')) {
      ++index_;
    }
    name = std::string(text_.substr(start, index_ - start));
    skip();
    if (peek() == '[') {
      ++index_;
      has_subscript = true;
      subscript = parse_comma();
      skip();
      if (peek() != ']') {
        error_ = true;
        return false;
      }
      ++index_;
    } else {
      has_subscript = false;
    }
    return true;
  }

  long long parse_conditional() {
    long long cond = parse_logical_or();
    skip();
    if (match("?")) {
      const long long if_true = parse_comma();
      skip();
      if (!match(":")) {
        error_ = true;
        return 0;
      }
      const long long if_false = parse_conditional();
      return cond != 0 ? if_true : if_false;
    }
    return cond;
  }

  long long parse_logical_or() {
    long long value = parse_logical_and();
    while (match("||")) {
      const long long right = parse_logical_and();
      value = (value != 0 || right != 0) ? 1 : 0;
    }
    return value;
  }

  long long parse_logical_and() {
    long long value = parse_bitwise_or();
    while (match("&&")) {
      const long long right = parse_bitwise_or();
      value = (value != 0 && right != 0) ? 1 : 0;
    }
    return value;
  }

  long long parse_bitwise_or() {
    long long value = parse_bitwise_xor();
    while (peek() == '|' && (index_ + 1 >= text_.size() || text_[index_ + 1] != '|')) {
      ++index_;
      const long long right = parse_bitwise_xor();
      value |= right;
    }
    return value;
  }

  long long parse_bitwise_xor() {
    long long value = parse_bitwise_and();
    while (peek() == '^') {
      ++index_;
      const long long right = parse_bitwise_and();
      value ^= right;
    }
    return value;
  }

  long long parse_bitwise_and() {
    long long value = parse_equality();
    while (peek() == '&' && (index_ + 1 >= text_.size() || text_[index_ + 1] != '&')) {
      ++index_;
      const long long right = parse_equality();
      value &= right;
    }
    return value;
  }

  long long parse_equality() {
    long long value = parse_relational();
    while (true) {
      if (match("==")) {
        value = (value == parse_relational()) ? 1 : 0;
      } else if (match("!=")) {
        value = (value != parse_relational()) ? 1 : 0;
      } else {
        break;
      }
    }
    return value;
  }

  long long parse_relational() {
    long long value = parse_shift();
    while (true) {
      if (match("<=")) {
        value = (value <= parse_shift()) ? 1 : 0;
      } else if (match(">=")) {
        value = (value >= parse_shift()) ? 1 : 0;
      } else if (peek() == '<' && (index_ + 1 >= text_.size() || text_[index_ + 1] != '<')) {
        ++index_;
        value = (value < parse_shift()) ? 1 : 0;
      } else if (peek() == '>' && (index_ + 1 >= text_.size() || text_[index_ + 1] != '>')) {
        ++index_;
        value = (value > parse_shift()) ? 1 : 0;
      } else {
        break;
      }
    }
    return value;
  }

  long long parse_shift() {
    long long value = parse_additive();
    while (true) {
      if (match("<<")) {
        const long long right = parse_additive();
        value = value << right;
      } else if (match(">>")) {
        const long long right = parse_additive();
        value = value >> right;
      } else {
        break;
      }
    }
    return value;
  }

  long long parse_additive() {
    long long value = parse_multiplicative();
    while (true) {
      skip();
      if (peek() == '+' && (index_ + 1 >= text_.size() || (text_[index_ + 1] != '+' && text_[index_ + 1] != '='))) {
        ++index_;
        const long long right = parse_multiplicative();
        if (__builtin_add_overflow(value, right, &value)) error_ = true;
      } else if (peek() == '-' && (index_ + 1 >= text_.size() || (text_[index_ + 1] != '-' && text_[index_ + 1] != '='))) {
        ++index_;
        const long long right = parse_multiplicative();
        if (__builtin_sub_overflow(value, right, &value)) error_ = true;
      } else {
        break;
      }
    }
    return value;
  }

  long long parse_multiplicative() {
    long long value = parse_power();
    while (true) {
      skip();
      if (peek() == '*' && (index_ + 1 >= text_.size() || (text_[index_ + 1] != '*' && text_[index_ + 1] != '='))) {
        ++index_;
        const long long right = parse_power();
        if (__builtin_mul_overflow(value, right, &value)) error_ = true;
      } else if (peek() == '/' && (index_ + 1 >= text_.size() || text_[index_ + 1] != '=')) {
        ++index_;
        const long long right = parse_power();
        if (right == 0) { error_ = true; return 0; }
        value /= right;
      } else if (peek() == '%' && (index_ + 1 >= text_.size() || text_[index_ + 1] != '=')) {
        ++index_;
        const long long right = parse_power();
        if (right == 0) { error_ = true; return 0; }
        value %= right;
      } else {
        break;
      }
    }
    return value;
  }

  long long parse_power() {
    long long value = parse_unary();
    if (match("**")) {
      const long long exponent = parse_power();
      if (exponent < 0) return 0;
      long long result = 1;
      long long base = value;
      long long exp = exponent;
      while (exp > 0) {
        if (exp % 2 == 1) result *= base;
        base *= base;
        exp /= 2;
      }
      return result;
    }
    return value;
  }

  long long parse_unary() {
    skip();
    if (match("++")) {
      std::string name;
      bool has_subscript = false;
      long long subscript = 0;
      if (!parse_variable_target(name, has_subscript, subscript)) {
        error_ = true;
        return 0;
      }
      long long val = get_var(name, has_subscript, subscript) + 1;
      set_var(name, has_subscript, subscript, val);
      return val;
    }
    if (match("--")) {
      std::string name;
      bool has_subscript = false;
      long long subscript = 0;
      if (!parse_variable_target(name, has_subscript, subscript)) {
        error_ = true;
        return 0;
      }
      long long val = get_var(name, has_subscript, subscript) - 1;
      set_var(name, has_subscript, subscript, val);
      return val;
    }
    if (match("+")) return parse_unary();
    if (match("-")) return -parse_unary();
    if (match("~")) return ~parse_unary();
    if (match("!")) return parse_unary() == 0 ? 1 : 0;
    return parse_postfix();
  }

  long long parse_postfix() {
    const std::size_t start = index_;
    std::string name;
    bool has_subscript = false;
    long long subscript = 0;
    if (parse_variable_target(name, has_subscript, subscript)) {
      if (match("++")) {
        const long long val = get_var(name, has_subscript, subscript);
        set_var(name, has_subscript, subscript, val + 1);
        return val;
      }
      if (match("--")) {
        const long long val = get_var(name, has_subscript, subscript);
        set_var(name, has_subscript, subscript, val - 1);
        return val;
      }
      index_ = start;
    }
    return parse_primary();
  }

  long long get_var(const std::string& name, bool has_subscript, long long subscript) {
    if (state_ == nullptr) return 0;
    std::string str_val = has_subscript
                              ? state_->get_array_element(name, static_cast<std::size_t>(subscript))
                              : state_->value(name);
    if (str_val.empty()) return 0;
    long long result = 0;
    ArithmeticParser(str_val, state_).run(result);
    return result;
  }

  void set_var(const std::string& name, bool has_subscript, long long subscript, long long val) {
    if (state_ == nullptr) return;
    if (has_subscript) {
      state_->set_array_element(name, static_cast<std::size_t>(subscript), std::to_string(val));
    } else {
      state_->set(name, std::to_string(val));
    }
  }

  long long parse_primary() {
    skip();
    if (match("(")) {
      const long long value = parse_comma();
      skip();
      if (!match(")")) {
        error_ = true;
        return 0;
      }
      return value;
    }

    if (index_ < text_.size() && (std::isalpha(static_cast<unsigned char>(text_[index_])) || text_[index_] == '_')) {
      std::string name;
      bool has_subscript = false;
      long long subscript = 0;
      if (parse_variable_target(name, has_subscript, subscript)) {
        return get_var(name, has_subscript, subscript);
      }
      error_ = true;
      return 0;
    }

    const std::size_t start = index_;
    if (match("0x") || match("0X")) {
      const std::size_t hex_start = index_;
      while (index_ < text_.size() && std::isxdigit(static_cast<unsigned char>(text_[index_])) != 0) ++index_;
      if (hex_start == index_) { error_ = true; return 0; }
      try {
        return std::stoll(std::string(text_.substr(hex_start, index_ - hex_start)), nullptr, 16);
      } catch (...) {
        error_ = true;
        return 0;
      }
    }
    if (match("0b") || match("0B")) {
      const std::size_t bin_start = index_;
      while (index_ < text_.size() && (text_[index_] == '0' || text_[index_] == '1')) ++index_;
      if (bin_start == index_) { error_ = true; return 0; }
      long long val = 0;
      for (std::size_t i = bin_start; i < index_; ++i) {
        val = (val << 1) | (text_[i] - '0');
      }
      return val;
    }

    while (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) ++index_;
    if (start == index_) {
      error_ = true;
      return 0;
    }

    if (peek() == '#') {
      const std::string base_str(text_.substr(start, index_ - start));
      int base = 10;
      try {
        base = std::stoi(base_str);
      } catch (...) {
        error_ = true;
        return 0;
      }
      ++index_;
      long long val = 0;
      const std::size_t val_start = index_;
      while (index_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[index_])) || text_[index_] == '@' || text_[index_] == '_')) {
        char ch = text_[index_];
        int digit = 0;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'z') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') digit = ch - 'A' + 36;
        else if (ch == '@') digit = 62;
        else if (ch == '_') digit = 63;
        else break;
        if (digit >= base) { error_ = true; return 0; }
        val = val * base + digit;
        ++index_;
      }
      if (val_start == index_) { error_ = true; return 0; }
      return val;
    }

    const std::string num_str(text_.substr(start, index_ - start));
    try {
      if (num_str.size() > 1 && num_str.front() == '0') {
        return std::stoll(num_str, nullptr, 8);
      }
      return std::stoll(num_str, nullptr, 10);
    } catch (...) {
      error_ = true;
      return 0;
    }
  }

  std::string_view text_;
  std::size_t index_{0};
  bool error_{false};
  ShellState* state_{nullptr};
};

}  // namespace

bool evaluate_arithmetic(std::string_view expression, long long& value, ShellState* state) {
  return ArithmeticParser(expression, state).run(value);
}

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

std::vector<std::string> Expander::brace_expand(std::string_view word) const {
  std::size_t first_open = std::string_view::npos;
  int depth = 0;
  for (std::size_t i = 0; i < word.size(); ++i) {
    if (word[i] == '\\' && i + 1 < word.size()) {
      ++i;
      continue;
    }
    if (word[i] == '{') {
      if (depth == 0) first_open = i;
      ++depth;
    } else if (word[i] == '}' && depth > 0) {
      --depth;
      if (depth == 0) {
        const std::size_t close = i;
        const std::string_view inner = word.substr(first_open + 1, close - first_open - 1);

        const std::size_t dotdot = inner.find("..");
        if (dotdot != std::string_view::npos) {
          const std::size_t dotdot2 = inner.find("..", dotdot + 2);
          const std::string_view start_str = inner.substr(0, dotdot);
          const std::string_view end_str = dotdot2 == std::string_view::npos
                                               ? inner.substr(dotdot + 2)
                                               : inner.substr(dotdot + 2, dotdot2 - dotdot - 2);
          long long step = 1;
          if (dotdot2 != std::string_view::npos) {
            try {
              step = std::stoll(std::string(inner.substr(dotdot2 + 2)));
              if (step == 0) step = 1;
              if (step < 0) step = -step;
            } catch (...) {
              step = 1;
            }
          }

          bool is_numeric = true;
          for (std::size_t idx = 0; idx < start_str.size(); ++idx) {
            if (idx == 0 && (start_str[idx] == '-' || start_str[idx] == '+')) continue;
            if (!std::isdigit(static_cast<unsigned char>(start_str[idx]))) { is_numeric = false; break; }
          }
          for (std::size_t idx = 0; idx < end_str.size(); ++idx) {
            if (idx == 0 && (end_str[idx] == '-' || end_str[idx] == '+')) continue;
            if (!std::isdigit(static_cast<unsigned char>(end_str[idx]))) { is_numeric = false; break; }
          }

          if (is_numeric && !start_str.empty() && !end_str.empty()) {
            try {
              const long long start_num = std::stoll(std::string(start_str));
              const long long end_num = std::stoll(std::string(end_str));
              const bool pad = (start_str.starts_with("0") && start_str.size() > 1) ||
                               (end_str.starts_with("0") && end_str.size() > 1);
              const std::size_t width = std::max(start_str.size(), end_str.size());

              std::vector<std::string> sequence;
              if (start_num <= end_num) {
                for (long long v = start_num; v <= end_num; v += step) {
                  std::string s = std::to_string(v);
                  if (pad && s.size() < width) s.insert(0, width - s.size(), '0');
                  sequence.push_back(std::move(s));
                }
              } else {
                for (long long v = start_num; v >= end_num; v -= step) {
                  std::string s = std::to_string(v);
                  if (pad && s.size() < width) s.insert(0, width - s.size(), '0');
                  sequence.push_back(std::move(s));
                }
              }

              std::vector<std::string> results;
              const std::string_view prefix = word.substr(0, first_open);
              const std::string_view suffix = word.substr(close + 1);
              for (const auto& item : sequence) {
                const std::string combined = std::string(prefix) + item + std::string(suffix);
                auto expanded = brace_expand(combined);
                results.insert(results.end(), expanded.begin(), expanded.end());
              }
              return results;
            } catch (...) {}
          } else if (start_str.size() == 1 && end_str.size() == 1 &&
                     std::isalpha(static_cast<unsigned char>(start_str.front())) &&
                     std::isalpha(static_cast<unsigned char>(end_str.front()))) {
            const char start_c = start_str.front();
            const char end_c = end_str.front();
            std::vector<std::string> sequence;
            if (start_c <= end_c) {
              for (char c = start_c; c <= end_c; c = static_cast<char>(c + step)) {
                sequence.push_back(std::string(1, c));
              }
            } else {
              for (char c = start_c; c >= end_c; c = static_cast<char>(c - step)) {
                sequence.push_back(std::string(1, c));
              }
            }
            std::vector<std::string> results;
            const std::string_view prefix = word.substr(0, first_open);
            const std::string_view suffix = word.substr(close + 1);
            for (const auto& item : sequence) {
              const std::string combined = std::string(prefix) + item + std::string(suffix);
              auto expanded = brace_expand(combined);
              results.insert(results.end(), expanded.begin(), expanded.end());
            }
            return results;
          }
        }

        std::vector<std::string> alternatives;
        std::size_t alt_start = 0;
        int inner_depth = 0;
        for (std::size_t j = 0; j < inner.size(); ++j) {
          if (inner[j] == '{') ++inner_depth;
          else if (inner[j] == '}') --inner_depth;
          else if (inner[j] == ',' && inner_depth == 0) {
            alternatives.emplace_back(inner.substr(alt_start, j - alt_start));
            alt_start = j + 1;
          }
        }
        if (!alternatives.empty() || alt_start > 0) {
          alternatives.emplace_back(inner.substr(alt_start));
          std::vector<std::string> results;
          const std::string_view prefix = word.substr(0, first_open);
          const std::string_view suffix = word.substr(close + 1);
          for (const auto& alt : alternatives) {
            const std::string combined = std::string(prefix) + alt + std::string(suffix);
            auto expanded = brace_expand(combined);
            results.insert(results.end(), expanded.begin(), expanded.end());
          }
          return results;
        }
      }
    }
  }
  return {std::string(word)};
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

  if (expression.starts_with("#") && expression.size() > 1) {
    const std::string_view target_name = expression.substr(1);
    if (target_name == "@" || target_name == "*") {
      return std::to_string(state_.positional.size());
    }
    const std::size_t bracket = target_name.find('[');
    if (bracket != std::string_view::npos && target_name.ends_with("]")) {
      const std::string base(target_name.substr(0, bracket));
      const std::string_view sub = target_name.substr(bracket + 1, target_name.size() - bracket - 2);
      if (sub == "@" || sub == "*") {
        return std::to_string(state_.array_length(base));
      }
      long long idx = 0;
      if (evaluate_arithmetic(sub, idx, const_cast<ShellState*>(&state_))) {
        return std::to_string(state_.get_array_element(base, static_cast<std::size_t>(idx)).size());
      }
    }
    const auto* variable = state_.find(target_name);
    return std::to_string(variable == nullptr ? 0 : variable->value.size());
  }

  if (expression.starts_with("!") && expression.size() > 1) {
    const std::string_view target_name = expression.substr(1);
    const std::size_t bracket = target_name.find('[');
    if (bracket != std::string_view::npos && target_name.ends_with("]")) {
      const std::string base(target_name.substr(0, bracket));
      const std::string_view sub = target_name.substr(bracket + 1, target_name.size() - bracket - 2);
      if (sub == "@" || sub == "*") {
        const auto* arr = state_.get_array(base);
        if (arr != nullptr) {
          std::vector<std::string> indices;
          for (std::size_t i = 0; i < arr->size(); ++i) indices.push_back(std::to_string(i));
          return join_values(indices, " ");
        }
        const auto* assoc = state_.get_assoc_array(base);
        if (assoc != nullptr) {
          std::vector<std::string> keys;
          for (const auto& [k, v] : *assoc) keys.push_back(k);
          return join_values(keys, " ");
        }
        return {};
      }
    }
    const auto* variable = state_.find(target_name);
    if (variable != nullptr) {
      const auto* indirect_var = state_.find(variable->value);
      defined = indirect_var != nullptr;
      return indirect_var == nullptr ? std::string{} : indirect_var->value;
    }
    defined = false;
    return {};
  }

  std::string var_name;
  std::string current;
  bool is_subscripted = false;
  std::size_t expr_cursor = 0;

  const std::size_t bracket_pos = expression.find('[');
  if (bracket_pos != std::string_view::npos) {
    const std::size_t close_bracket = expression.find(']', bracket_pos);
    if (close_bracket != std::string_view::npos) {
      var_name = std::string(expression.substr(0, bracket_pos));
      const std::string_view sub = expression.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
      is_subscripted = true;
      expr_cursor = close_bracket + 1;
      if (sub == "@" || sub == "*") {
        const auto* arr = state_.get_array(var_name);
        if (arr != nullptr) {
          const std::string ifs = state_.value("IFS");
          current = join_values(*arr, ifs.empty() ? " " : ifs.substr(0, 1));
          defined = true;
        } else {
          current = state_.value(var_name);
          defined = state_.is_set(var_name);
        }
      } else {
        long long idx = 0;
        if (evaluate_arithmetic(sub, idx, const_cast<ShellState*>(&state_))) {
          current = state_.get_array_element(var_name, static_cast<std::size_t>(idx));
          defined = !current.empty() || state_.is_array(var_name);
        } else {
          current = state_.get_assoc_element(var_name, sub);
          defined = !current.empty();
        }
      }
    }
  }

  if (!is_subscripted) {
    std::string_view name_view = expression;
    std::string_view operation_view;
    const std::string_view operators[] = {
        ":-", "-", ":=", "=", ":?", "?", ":+", "+", "##", "#", "%%", "%",
        "//", "/", "^^", "^", ",,", ","};
    for (const auto candidate : operators) {
      const std::size_t position = expression.find(candidate);
      if (position != std::string_view::npos) {
        name_view = expression.substr(0, position);
        operation_view = expression.substr(position);
        break;
      }
    }
    if (operation_view.empty()) {
      const std::size_t colon_pos = expression.find(':');
      if (colon_pos != std::string_view::npos) {
        name_view = expression.substr(0, colon_pos);
        operation_view = expression.substr(colon_pos);
      }
    }

    if (!valid_name(name_view)) {
      error = "malformed parameter expansion name";
      return {};
    }
    var_name = std::string(name_view);
    const auto* variable = state_.find(var_name);
    defined = variable != nullptr;
    current = variable == nullptr ? std::string{} : variable->value;

    if (operation_view.empty()) {
      if (!defined && state_.option(ShellOption::Nounset)) {
        error = "parameter is unset: " + var_name;
      }
      return current;
    }

    if (operation_view.starts_with(":")) {
      const char second = operation_view.size() > 1 ? operation_view[1] : '\0';
      if (second != '-' && second != '=' && second != '?' && second != '+') {
        const std::string_view sub = operation_view.substr(1);
        const std::size_t colon2 = sub.find(':');
        long long offset = 0;
        long long len = -1;
        if (colon2 != std::string_view::npos) {
          evaluate_arithmetic(sub.substr(0, colon2), offset, const_cast<ShellState*>(&state_));
          evaluate_arithmetic(sub.substr(colon2 + 1), len, const_cast<ShellState*>(&state_));
        } else {
          evaluate_arithmetic(sub, offset, const_cast<ShellState*>(&state_));
        }
        const long long cur_len = static_cast<long long>(current.size());
        if (offset < 0) offset = cur_len + offset;
        if (offset < 0) offset = 0;
        if (offset > cur_len) return {};
        if (len < 0) return current.substr(static_cast<std::size_t>(offset));
        return current.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(len));
      }
    }

    if (operation_view.starts_with("/")) {
      const bool all = operation_view.starts_with("//");
      const std::string_view rest = operation_view.substr(all ? 2 : 1);
      const std::size_t slash2 = rest.find('/');
      const std::string_view pattern = slash2 == std::string_view::npos ? rest : rest.substr(0, slash2);
      const std::string replacement = slash2 == std::string_view::npos ? std::string{} : std::string(rest.substr(slash2 + 1));

      if (pattern.empty()) return current;

      std::string result = current;
      if (pattern.starts_with("#")) {
        const std::string pat(pattern.substr(1));
        for (std::size_t len = result.size(); len > 0; --len) {
          if (fnmatch(pat.c_str(), result.substr(0, len).c_str(), 0) == 0) {
            return replacement + result.substr(len);
          }
        }
        return result;
      }
      if (pattern.starts_with("%")) {
        const std::string pat(pattern.substr(1));
        for (std::size_t len = result.size(); len > 0; --len) {
          if (fnmatch(pat.c_str(), result.substr(result.size() - len).c_str(), 0) == 0) {
            return result.substr(0, result.size() - len) + replacement;
          }
        }
        return result;
      }

      const std::string pat(pattern);
      std::size_t pos = 0;
      while (pos < result.size()) {
        bool matched = false;
        for (std::size_t len = result.size() - pos; len > 0; --len) {
          if (fnmatch(pat.c_str(), result.substr(pos, len).c_str(), 0) == 0) {
            result.replace(pos, len, replacement);
            pos += replacement.size();
            matched = true;
            if (!all) return result;
            break;
          }
        }
        if (!matched) ++pos;
      }
      return result;
    }

    if (operation_view.starts_with("^^")) {
      std::string result = current;
      for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return result;
    }
    if (operation_view.starts_with("^")) {
      std::string result = current;
      if (!result.empty()) result.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(result.front())));
      return result;
    }
    if (operation_view.starts_with(",,")) {
      std::string result = current;
      for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return result;
    }
    if (operation_view.starts_with(",")) {
      std::string result = current;
      if (!result.empty()) result.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(result.front())));
      return result;
    }

    const bool colon = operation_view.front() == ':';
    const char action = operation_view[colon ? 1 : 0];
    const std::string operand(operation_view.substr(colon ? 2 : 1));
    const bool use_operand = !defined || (colon && current.empty());
    switch (action) {
      case '-': return use_operand ? operand : current;
      case '=':
        if (!use_operand) return current;
        if (!state_.set(var_name, operand)) {
          error = "cannot assign readonly parameter: " + var_name;
          return {};
        }
        return operand;
      case '?':
        if (use_operand) {
          error = operand.empty() ? "parameter is required: " + var_name : operand;
          return {};
        }
        return current;
      case '+': return use_operand ? std::string{} : operand;
      case '#': return trim_pattern(current, operand, operation_view.starts_with("##"), true);
      case '%': return trim_pattern(current, operand, operation_view.starts_with("%%"), false);
      default: error = "unsupported parameter operator"; return {};
    }
  }

  if (expr_cursor >= expression.size()) return current;
  return current;
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
    if (fragment[index + 1] == '(' && index + 2 < fragment.size() && fragment[index + 2] == '(') {
      const std::size_t end = fragment.find("))", index + 3);
      if (end == std::string_view::npos) {
        error = "unterminated arithmetic expansion";
        return {};
      }
      long long value_number = 0;
      if (!evaluate_arithmetic(fragment.substr(index + 3, end - index - 3), value_number, &state_)) {
        error = "invalid arithmetic expression";
        return {};
      }
      result += std::to_string(value_number);
      index = end + 2;
      had_quoted = had_quoted || quoted;
      had_unquoted = had_unquoted || !quoted;
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
    std::size_t end = index + 2;
    std::string expression;
    if (fragment[index + 1] == '{') {
      int brace_depth = 1;
      std::size_t close = index + 2;
      while (close < fragment.size() && brace_depth > 0) {
        if (fragment[close] == '{') ++brace_depth;
        else if (fragment[close] == '}') --brace_depth;
        ++close;
      }
      if (brace_depth != 0) {
        error = "unterminated parameter expansion";
        return {};
      }
      expression = std::string(fragment.substr(index + 2, close - index - 3));
      end = close;
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
    bool has_braces = false;
    for (const auto& part : word_value.parts) {
      if (part.kind == syntax::WordPartKind::Literal && part.text.find('{') != std::string::npos) {
        has_braces = true;
        break;
      }
    }
    if (has_braces) {
      std::string raw;
      for (const auto& part : word_value.parts) raw += part.text;
      const auto expanded_braces = brace_expand(raw);
      if (expanded_braces.size() > 1) {
        for (const auto& expanded_str : expanded_braces) {
          syntax::Word alt_word;
          syntax::WordPart part;
          part.kind = syntax::WordPartKind::Literal;
          part.text = expanded_str;
          alt_word.parts.push_back(std::move(part));
          auto expanded = word(alt_word, context);
          if (!expanded.ok()) return expanded;
          result.fields.insert(result.fields.end(), expanded.fields.begin(), expanded.fields.end());
        }
        continue;
      }
    }
    auto expanded = word(word_value, context);
    if (!expanded.ok()) return expanded;
    result.fields.insert(result.fields.end(), expanded.fields.begin(), expanded.fields.end());
  }
  return result;
}

}  // namespace splice::expand
