#include "expand/state.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern char** environ;

namespace splice::expand {

ShellState::ShellState() : shell_pid(static_cast<long long>(getpid())) {
  if (const char* directory = std::getenv("PWD"); directory != nullptr) {
    current_directory = directory;
  }
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    const std::string_view record(*entry);
    const std::size_t separator = record.find('=');
    if (separator == std::string_view::npos) continue;
    variables_.emplace(std::string(record.substr(0, separator)),
                       Variable{std::string(record.substr(separator + 1)), true, false});
  }
  if (!is_set("IFS")) set("IFS", " \t\n");
}

const Variable* ShellState::find(std::string_view name) const {
  const auto iterator = variables_.find(std::string(name));
  return iterator == variables_.end() ? nullptr : &iterator->second;
}

std::string ShellState::value(std::string_view name) const {
  if (const auto* variable = find(name); variable != nullptr) return variable->value;
  return {};
}

bool ShellState::is_set(std::string_view name) const { return find(name) != nullptr; }

bool ShellState::is_exported(std::string_view name) const {
  const auto* variable = find(name);
  return variable != nullptr && variable->exported;
}

bool ShellState::set(std::string name, std::string value, bool exported) {
  auto iterator = variables_.find(name);
  if (iterator != variables_.end() && iterator->second.readonly) return false;
  if (iterator == variables_.end()) {
    variables_.emplace(std::move(name), Variable{std::move(value), exported, false});
  } else {
    iterator->second.value = std::move(value);
    iterator->second.exported = iterator->second.exported || exported;
  }
  return true;
}

bool ShellState::unset(std::string_view name) {
  const auto iterator = variables_.find(std::string(name));
  if (iterator == variables_.end()) return true;
  if (iterator->second.readonly) return false;
  variables_.erase(iterator);
  return true;
}

bool ShellState::mark_exported(std::string_view name, bool exported) {
  auto iterator = variables_.find(std::string(name));
  if (iterator == variables_.end()) {
    variables_.emplace(std::string(name), Variable{"", exported, false});
    return true;
  }
  if (iterator->second.readonly && !exported) return false;
  iterator->second.exported = exported;
  return true;
}

bool ShellState::mark_readonly(std::string_view name) {
  auto iterator = variables_.find(std::string(name));
  if (iterator == variables_.end()) {
    variables_.emplace(std::string(name), Variable{"", false, true});
  } else {
    iterator->second.readonly = true;
  }
  return true;
}

std::vector<std::string> ShellState::environment() const {
  std::vector<std::string> result;
  result.reserve(variables_.size());
  for (const auto& [name, variable] : variables_) {
    if (variable.exported) result.push_back(name + '=' + variable.value);
  }
  return result;
}

bool ShellState::set_option(ShellOption option, bool enabled) {
  if (enabled) options_.insert(option);
  else options_.erase(option);
  return true;
}

bool ShellState::option(ShellOption option) const noexcept {
  return options_.contains(option);
}

const char* profile_name(Profile profile) noexcept {
  switch (profile) {
    case Profile::Posix: return "posix";
    case Profile::Bash: return "bash";
    case Profile::Splice: return "splice";
  }
  return "unknown";
}

bool parse_profile(std::string_view text, Profile& profile) noexcept {
  if (text == "posix") profile = Profile::Posix;
  else if (text == "bash") profile = Profile::Bash;
  else if (text == "splice") profile = Profile::Splice;
  else return false;
  return true;
}

bool valid_name(std::string_view name) noexcept {
  if (name.empty()) return false;
  if (!(name.front() == '_' || std::isalpha(static_cast<unsigned char>(name.front())))) return false;
  for (const char character : name.substr(1)) {
    if (!(character == '_' || std::isalnum(static_cast<unsigned char>(character)))) return false;
  }
  return true;
}

}  // namespace splice::expand
