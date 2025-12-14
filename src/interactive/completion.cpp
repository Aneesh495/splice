#include "interactive/completion.hpp"

#include "builtins/builtins.hpp"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <set>

extern "C" char** environ;

namespace splice::interactive {
namespace {

bool is_delimiter(char ch) noexcept {
  return std::isspace(static_cast<unsigned char>(ch)) ||
         ch == ';' || ch == '|' || ch == '&' || ch == '(' || ch == ')' ||
         ch == '<' || ch == '>' || ch == '`';
}

}  // namespace

Completer::Completer(const expand::ShellState& state) : state_(state) {}

std::string Completer::longest_common_prefix(const std::vector<Candidate>& candidates) const {
  if (candidates.empty()) return {};
  std::string prefix = candidates.front().text;
  for (std::size_t i = 1; i < candidates.size(); ++i) {
    const std::string& current = candidates[i].text;
    std::size_t j = 0;
    while (j < prefix.size() && j < current.size() && prefix[j] == current[j]) {
      ++j;
    }
    prefix.resize(j);
    if (prefix.empty()) break;
  }
  return prefix;
}

std::vector<Candidate> Completer::complete_commands(std::string_view prefix) const {
  std::vector<Candidate> results;
  std::set<std::string> seen;

  for (const auto& name : builtins::names()) {
    if (name.starts_with(prefix) && !seen.contains(name)) {
      seen.insert(name);
      results.push_back(Candidate{name, name, CompletionKind::Builtin, "builtin"});
    }
  }

  for (const auto& [name, func] : state_.functions) {
    if (name.starts_with(prefix) && !seen.contains(name)) {
      seen.insert(name);
      results.push_back(Candidate{name, name, CompletionKind::Command, "function"});
    }
  }

  for (const auto& [name, val] : state_.aliases) {
    if (name.starts_with(prefix) && !seen.contains(name)) {
      seen.insert(name);
      results.push_back(Candidate{name, name, CompletionKind::Command, "alias"});
    }
  }

  const std::string path_env = state_.value("PATH");
  std::size_t start = 0;
  while (start <= path_env.size()) {
    const std::size_t sep = path_env.find(':', start);
    const std::string dir = path_env.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
    const std::string target_dir = dir.empty() ? "." : dir;

    DIR* dp = opendir(target_dir.c_str());
    if (dp != nullptr) {
      struct dirent* entry = nullptr;
      while ((entry = readdir(dp)) != nullptr) {
        const std::string_view d_name(entry->d_name);
        if (d_name.starts_with(prefix) && !seen.contains(entry->d_name)) {
          const std::string full_path = target_dir + "/" + entry->d_name;
          if (access(full_path.c_str(), X_OK) == 0) {
            struct stat st{};
            if (stat(full_path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
              seen.insert(entry->d_name);
              results.push_back(Candidate{entry->d_name, entry->d_name, CompletionKind::Command, "external command"});
            }
          }
        }
      }
      closedir(dp);
    }

    if (sep == std::string::npos) break;
    start = sep + 1;
  }

  std::sort(results.begin(), results.end(), [](const Candidate& a, const Candidate& b) {
    return a.text < b.text;
  });
  return results;
}

std::vector<Candidate> Completer::complete_files(std::string_view prefix, bool directories_only) const {
  std::vector<Candidate> results;
  std::string expanded_prefix(prefix);
  std::string home_expanded;

  if (expanded_prefix.starts_with("~/")) {
    const std::string home = state_.value("HOME");
    home_expanded = home + expanded_prefix.substr(1);
    expanded_prefix = home_expanded;
  } else if (expanded_prefix == "~") {
    const std::string home = state_.value("HOME");
    return {Candidate{"~/", "~/", CompletionKind::Directory, "home directory"}};
  }

  std::string dir_part = ".";
  std::string file_part = expanded_prefix;
  const std::size_t last_slash = expanded_prefix.rfind('/');
  if (last_slash != std::string::npos) {
    dir_part = expanded_prefix.substr(0, last_slash == 0 ? 1 : last_slash);
    file_part = expanded_prefix.substr(last_slash + 1);
  }

  DIR* dp = opendir(dir_part.c_str());
  if (dp == nullptr) return results;

  struct dirent* entry = nullptr;
  while ((entry = readdir(dp)) != nullptr) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") continue;
    if (!file_part.starts_with(".") && name.starts_with(".")) continue;
    if (!name.starts_with(file_part)) continue;

    const std::string full_path = (dir_part == "/" ? "/" : (dir_part + "/")) + entry->d_name;
    struct stat st{};
    if (stat(full_path.c_str(), &st) != 0) continue;

    const bool is_dir = S_ISDIR(st.st_mode);
    if (directories_only && !is_dir) continue;

    std::string candidate_text;
    if (last_slash != std::string::npos) {
      candidate_text = std::string(prefix.substr(0, last_slash + 1)) + entry->d_name;
    } else {
      candidate_text = entry->d_name;
    }

    if (is_dir) {
      candidate_text.push_back('/');
      results.push_back(Candidate{candidate_text, std::string(entry->d_name) + "/", CompletionKind::Directory, "directory"});
    } else {
      results.push_back(Candidate{candidate_text, entry->d_name, CompletionKind::File, "file"});
    }
  }
  closedir(dp);

  std::sort(results.begin(), results.end(), [](const Candidate& a, const Candidate& b) {
    return a.text < b.text;
  });
  return results;
}

std::vector<Candidate> Completer::complete_variables(std::string_view prefix) const {
  std::vector<Candidate> results;
  std::set<std::string> seen;

  bool has_brace = false;
  std::string_view var_prefix = prefix;
  if (var_prefix.starts_with("${")) {
    has_brace = true;
    var_prefix = var_prefix.substr(2);
  } else if (var_prefix.starts_with("$")) {
    var_prefix = var_prefix.substr(1);
  }

  auto add_var = [&](std::string_view vname, std::string_view desc) {
    if (vname.starts_with(var_prefix) && !seen.contains(std::string(vname))) {
      seen.insert(std::string(vname));
      std::string text = has_brace ? ("${" + std::string(vname) + "}") : ("$" + std::string(vname));
      results.push_back(Candidate{std::move(text), std::string(vname), CompletionKind::Variable, std::string(desc)});
    }
  };

  const std::string_view specials[] = {
      "?", "$", "!", "#", "@", "*", "0", "PWD", "OLDPWD", "PATH", "HOME", "IFS", "PIPESTATUS"};
  for (const auto& sp : specials) {
    add_var(sp, "special variable");
  }

  for (char** env = ::environ; env != nullptr && *env != nullptr; ++env) {
    const std::string_view entry(*env);
    const std::size_t eq = entry.find('=');
    if (eq != std::string_view::npos) {
      add_var(entry.substr(0, eq), "environment variable");
    }
  }

  for (const auto& [name, arr] : state_.arrays) {
    add_var(name, "array variable");
  }

  std::sort(results.begin(), results.end(), [](const Candidate& a, const Candidate& b) {
    return a.text < b.text;
  });
  return results;
}

CompletionResult Completer::complete(std::string_view line, std::size_t cursor) const {
  CompletionResult result;
  if (cursor > line.size()) cursor = line.size();

  std::size_t word_start = cursor;
  while (word_start > 0 && !is_delimiter(line[word_start - 1])) {
    --word_start;
  }

  const std::string_view current_word = line.substr(word_start, cursor - word_start);
  result.replacement_start = word_start;
  result.replacement_length = cursor - word_start;

  if (current_word.starts_with("$")) {
    result.candidates = complete_variables(current_word);
    result.common_prefix = longest_common_prefix(result.candidates);
    return result;
  }

  bool is_command_position = true;
  std::size_t scan = word_start;
  while (scan > 0) {
    --scan;
    if (std::isspace(static_cast<unsigned char>(line[scan]))) continue;
    if (line[scan] == ';' || line[scan] == '|' || line[scan] == '&' || line[scan] == '(' || line[scan] == '`') {
      is_command_position = true;
    } else {
      is_command_position = false;
    }
    break;
  }

  if (is_command_position && current_word.find('/') == std::string_view::npos) {
    result.candidates = complete_commands(current_word);
    if (result.candidates.empty()) {
      result.candidates = complete_files(current_word);
    }
  } else {
    result.candidates = complete_files(current_word);
  }

  result.common_prefix = longest_common_prefix(result.candidates);
  return result;
}

}  // namespace splice::interactive
