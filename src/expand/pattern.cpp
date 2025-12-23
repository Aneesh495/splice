#include "expand/pattern.hpp"

#include <cctype>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace splice::expand {
namespace {

bool is_posix_class(std::string_view name, char ch) {
  if (name == "alnum") return std::isalnum(static_cast<unsigned char>(ch)) != 0;
  if (name == "alpha") return std::isalpha(static_cast<unsigned char>(ch)) != 0;
  if (name == "digit") return std::isdigit(static_cast<unsigned char>(ch)) != 0;
  if (name == "space") return std::isspace(static_cast<unsigned char>(ch)) != 0;
  if (name == "upper") return std::isupper(static_cast<unsigned char>(ch)) != 0;
  if (name == "lower") return std::islower(static_cast<unsigned char>(ch)) != 0;
  if (name == "xdigit") return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
  if (name == "punct") return std::ispunct(static_cast<unsigned char>(ch)) != 0;
  if (name == "print") return std::isprint(static_cast<unsigned char>(ch)) != 0;
  return false;
}

}  // namespace

Pattern::Pattern(std::string_view pattern, bool extglob)
    : pattern_(pattern), extglob_(extglob) {}

bool Pattern::is_pattern(std::string_view text) noexcept {
  bool escaped = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (text[i] == '\\') {
      escaped = true;
      continue;
    }
    if (text[i] == '*' || text[i] == '?' || text[i] == '[') return true;
    if (i + 1 < text.size() && text[i + 1] == '(' &&
        (text[i] == '@' || text[i] == '!' || text[i] == '+' || text[i] == '?' || text[i] == '*')) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> Pattern::split_alternatives(std::string_view sub_patterns) const {
  std::vector<std::string> alts;
  std::size_t start = 0;
  int paren_depth = 0;
  for (std::size_t i = 0; i < sub_patterns.size(); ++i) {
    if (sub_patterns[i] == '\\' && i + 1 < sub_patterns.size()) {
      ++i;
      continue;
    }
    if (sub_patterns[i] == '(') {
      ++paren_depth;
    } else if (sub_patterns[i] == ')' && paren_depth > 0) {
      --paren_depth;
    } else if (sub_patterns[i] == '|' && paren_depth == 0) {
      alts.emplace_back(sub_patterns.substr(start, i - start));
      start = i + 1;
    }
  }
  alts.emplace_back(sub_patterns.substr(start));
  return alts;
}

bool Pattern::match_bracket(std::string_view content, char ch) const {
  if (content.empty()) return false;
  bool negate = false;
  std::size_t idx = 0;
  if (content[0] == '!' || content[0] == '^') {
    negate = true;
    idx = 1;
  }
  bool matched = false;
  while (idx < content.size()) {
    if (content.substr(idx).starts_with("[:")) {
      const std::size_t class_end = content.find(":]", idx + 2);
      if (class_end != std::string_view::npos) {
        const std::string_view class_name = content.substr(idx + 2, class_end - idx - 2);
        if (is_posix_class(class_name, ch)) {
          matched = true;
          break;
        }
        idx = class_end + 2;
        continue;
      }
    }
    if (idx + 2 < content.size() && content[idx + 1] == '-') {
      const char start_c = content[idx];
      const char end_c = content[idx + 2];
      if (ch >= start_c && ch <= end_c) {
        matched = true;
        break;
      }
      idx += 3;
    } else {
      if (content[idx] == ch) {
        matched = true;
        break;
      }
      ++idx;
    }
  }
  return negate ? !matched : matched;
}

bool Pattern::match_extglob(char op, std::string_view sub_patterns, std::string_view text,
                            std::size_t& consumed) const {
  const auto alts = split_alternatives(sub_patterns);
  if (op == '@') {
    for (const auto& alt : alts) {
      for (std::size_t len = text.size(); len > 0; --len) {
        if (match_internal(alt, text.substr(0, len))) {
          consumed = len;
          return true;
        }
      }
      if (match_internal(alt, "")) {
        consumed = 0;
        return true;
      }
    }
    return false;
  }
  if (op == '?') {
    if (match_extglob('@', sub_patterns, text, consumed)) return true;
    consumed = 0;
    return true;
  }
  if (op == '+') {
    for (const auto& alt : alts) {
      for (std::size_t len = text.size(); len > 0; --len) {
        if (match_internal(alt, text.substr(0, len))) {
          std::size_t more_consumed = 0;
          if (match_extglob('*', sub_patterns, text.substr(len), more_consumed)) {
            consumed = len + more_consumed;
            return true;
          }
          consumed = len;
          return true;
        }
      }
    }
    return false;
  }
  if (op == '*') {
    std::size_t plus_len = 0;
    if (match_extglob('+', sub_patterns, text, plus_len)) {
      consumed = plus_len;
      return true;
    }
    consumed = 0;
    return true;
  }
  if (op == '!') {
    for (std::size_t len = text.size(); len > 0; --len) {
      bool any_match = false;
      for (const auto& alt : alts) {
        if (match_internal(alt, text.substr(0, len))) {
          any_match = true;
          break;
        }
      }
      if (!any_match) {
        consumed = len;
        return true;
      }
    }
    consumed = 0;
    return true;
  }
  return false;
}

bool Pattern::match_internal(std::string_view pat, std::string_view text) const {
  std::size_t p = 0;
  std::size_t t = 0;

  while (p < pat.size()) {
    if (pat[p] == '\\' && p + 1 < pat.size()) {
      if (t >= text.size() || text[t] != pat[p + 1]) return false;
      p += 2;
      ++t;
      continue;
    }

    if (extglob_ && p + 1 < pat.size() && pat[p + 1] == '(' &&
        (pat[p] == '@' || pat[p] == '!' || pat[p] == '+' || pat[p] == '?' || pat[p] == '*')) {
      const char op = pat[p];
      int depth = 1;
      std::size_t close = p + 2;
      while (close < pat.size() && depth > 0) {
        if (pat[close] == '(') ++depth;
        else if (pat[close] == ')') --depth;
        ++close;
      }
      if (depth == 0) {
        const std::string_view sub_patterns = pat.substr(p + 2, close - p - 3);
        const std::string_view rest_pat = pat.substr(close);

        for (std::size_t len = text.size() - t; len > 0; --len) {
          std::size_t consumed = 0;
          if (match_extglob(op, sub_patterns, text.substr(t, len), consumed)) {
            if (match_internal(rest_pat, text.substr(t + consumed))) return true;
          }
        }
        std::size_t zero_consumed = 0;
        if (match_extglob(op, sub_patterns, "", zero_consumed)) {
          if (match_internal(rest_pat, text.substr(t))) return true;
        }
        return false;
      }
    }

    if (pat[p] == '*') {
      while (p + 1 < pat.size() && pat[p + 1] == '*') ++p;
      if (p + 1 == pat.size()) return true;
      const std::string_view rest_pat = pat.substr(p + 1);
      for (std::size_t next_t = t; next_t <= text.size(); ++next_t) {
        if (match_internal(rest_pat, text.substr(next_t))) return true;
      }
      return false;
    }

    if (pat[p] == '?') {
      if (t >= text.size()) return false;
      ++p;
      ++t;
      continue;
    }

    if (pat[p] == '[') {
      const std::size_t close = pat.find(']', p + 1);
      if (close != std::string_view::npos) {
        if (t >= text.size()) return false;
        const std::string_view content = pat.substr(p + 1, close - p - 1);
        if (!match_bracket(content, text[t])) return false;
        p = close + 1;
        ++t;
        continue;
      }
    }

    if (t >= text.size() || pat[p] != text[t]) return false;
    ++p;
    ++t;
  }
  return t == text.size();
}

bool Pattern::matches(std::string_view text) const {
  return match_internal(pattern_, text);
}

PatternMatchResult Pattern::find_match(std::string_view text, bool longest, bool prefix_only) const {
  PatternMatchResult result;
  if (prefix_only) {
    if (longest) {
      for (std::size_t len = text.size(); len > 0; --len) {
        if (match_internal(pattern_, text.substr(0, len))) {
          result.matched = true;
          result.match_start = 0;
          result.match_length = len;
          return result;
        }
      }
    } else {
      for (std::size_t len = 1; len <= text.size(); ++len) {
        if (match_internal(pattern_, text.substr(0, len))) {
          result.matched = true;
          result.match_start = 0;
          result.match_length = len;
          return result;
        }
      }
    }
    if (match_internal(pattern_, "")) {
      result.matched = true;
      result.match_start = 0;
      result.match_length = 0;
      return result;
    }
    return result;
  }

  for (std::size_t start = 0; start < text.size(); ++start) {
    if (longest) {
      for (std::size_t len = text.size() - start; len > 0; --len) {
        if (match_internal(pattern_, text.substr(start, len))) {
          result.matched = true;
          result.match_start = start;
          result.match_length = len;
          return result;
        }
      }
    } else {
      for (std::size_t len = 1; len <= text.size() - start; ++len) {
        if (match_internal(pattern_, text.substr(start, len))) {
          result.matched = true;
          result.match_start = start;
          result.match_length = len;
          return result;
        }
      }
    }
  }
  return result;
}

std::string Pattern::replace_first(std::string_view text, std::string_view replacement) const {
  const auto match = find_match(text, true, false);
  if (!match.matched) return std::string(text);
  std::string result(text.substr(0, match.match_start));
  result.append(replacement);
  result.append(text.substr(match.match_start + match.match_length));
  return result;
}

std::string Pattern::replace_all(std::string_view text, std::string_view replacement) const {
  std::string result;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto match = find_match(text.substr(cursor), false, false);
    if (!match.matched) {
      result.append(text.substr(cursor));
      break;
    }
    result.append(text.substr(cursor, match.match_start));
    result.append(replacement);
    cursor += match.match_start + (match.match_length == 0 ? 1 : match.match_length);
  }
  return result;
}

std::vector<std::string> Pattern::glob(std::string_view pattern_str) {
  std::vector<std::string> results;
  std::string dir_part = ".";
  std::string file_part(pattern_str);
  const std::size_t last_slash = pattern_str.rfind('/');
  if (last_slash != std::string_view::npos) {
    dir_part = pattern_str.substr(0, last_slash == 0 ? 1 : last_slash);
    file_part = pattern_str.substr(last_slash + 1);
  }

  DIR* dp = opendir(dir_part.c_str());
  if (dp == nullptr) return {std::string(pattern_str)};

  Pattern pat(file_part, true);
  struct dirent* entry = nullptr;
  while ((entry = readdir(dp)) != nullptr) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") continue;
    if (!file_part.starts_with(".") && name.starts_with(".")) continue;
    if (pat.matches(name)) {
      if (last_slash != std::string_view::npos) {
        results.push_back(std::string(pattern_str.substr(0, last_slash + 1)) + entry->d_name);
      } else {
        results.push_back(entry->d_name);
      }
    }
  }
  closedir(dp);

  if (results.empty()) return {std::string(pattern_str)};
  std::sort(results.begin(), results.end());
  return results;
}

bool pattern_match(std::string_view pattern, std::string_view text, bool extglob) {
  return Pattern(pattern, extglob).matches(text);
}

}  // namespace splice::expand
