#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace splice::expand {

struct PatternMatchResult {
  bool matched{false};
  std::size_t match_start{0};
  std::size_t match_length{0};
};

class Pattern {
 public:
  explicit Pattern(std::string_view pattern, bool extglob = true);

  [[nodiscard]] bool matches(std::string_view text) const;
  [[nodiscard]] PatternMatchResult find_match(std::string_view text, bool longest = true, bool prefix_only = false) const;
  [[nodiscard]] std::string replace_all(std::string_view text, std::string_view replacement) const;
  [[nodiscard]] std::string replace_first(std::string_view text, std::string_view replacement) const;

  [[nodiscard]] static bool is_pattern(std::string_view text) noexcept;
  [[nodiscard]] static std::vector<std::string> glob(std::string_view pattern_str);

 private:
  bool match_internal(std::string_view pat, std::string_view text) const;
  bool match_bracket(std::string_view bracket_content, char ch) const;
  bool match_extglob(char op, std::string_view sub_patterns, std::string_view text, std::size_t& consumed) const;
  std::vector<std::string> split_alternatives(std::string_view sub_patterns) const;

  std::string pattern_;
  bool extglob_{true};
};

bool pattern_match(std::string_view pattern, std::string_view text, bool extglob = true);

}  // namespace splice::expand
