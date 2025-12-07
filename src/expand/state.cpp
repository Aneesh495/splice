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
  for (auto it = function_frames.rbegin(); it != function_frames.rend(); ++it) {
    const auto local_it = it->local_vars.find(std::string(name));
    if (local_it != it->local_vars.end()) return &local_it->second;
  }
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

bool ShellState::is_readonly(std::string_view name) const {
  const auto* variable = find(name);
  return variable != nullptr && variable->readonly;
}

bool ShellState::set(std::string name, std::string value, bool exported) {
  for (auto it = function_frames.rbegin(); it != function_frames.rend(); ++it) {
    auto local_it = it->local_vars.find(name);
    if (local_it != it->local_vars.end()) {
      if (local_it->second.readonly) return false;
      local_it->second.value = std::move(value);
      local_it->second.exported = local_it->second.exported || exported;
      return true;
    }
  }
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

void ShellState::push_function_frame(std::vector<std::string> new_positional) {
  FunctionFrame frame;
  frame.positional = std::move(positional);
  positional = std::move(new_positional);
  function_frames.push_back(std::move(frame));
}

void ShellState::pop_function_frame() {
  if (function_frames.empty()) return;
  positional = std::move(function_frames.back().positional);
  function_frames.pop_back();
}

bool ShellState::set_local(std::string name, std::string value) {
  if (function_frames.empty()) return set(std::move(name), std::move(value));
  auto& frame = function_frames.back();
  auto it = frame.local_vars.find(name);
  if (it != frame.local_vars.end() && it->second.readonly) return false;
  if (it == frame.local_vars.end()) {
    frame.local_vars.emplace(std::move(name), Variable{std::move(value), false, false});
  } else {
    it->second.value = std::move(value);
  }
  return true;
}

bool ShellState::set_array_element(std::string_view name, std::size_t index, std::string value) {
  std::string key(name);
  auto& arr = arrays[key];
  if (index >= arr.size()) arr.resize(index + 1);
  arr[index] = value;
  if (index == 0) set(key, std::move(value));
  return true;
}

std::string ShellState::get_array_element(std::string_view name, std::size_t index) const {
  const auto it = arrays.find(std::string(name));
  if (it == arrays.end()) {
    if (index == 0) return value(name);
    return {};
  }
  if (index < it->second.size()) return it->second[index];
  return {};
}

bool ShellState::set_array(std::string name, std::vector<std::string> values) {
  if (!values.empty()) set(name, values.front());
  else set(name, "");
  arrays[std::move(name)] = std::move(values);
  return true;
}

const std::vector<std::string>* ShellState::get_array(std::string_view name) const {
  const auto it = arrays.find(std::string(name));
  return it == arrays.end() ? nullptr : &it->second;
}

bool ShellState::is_array(std::string_view name) const {
  return arrays.find(std::string(name)) != arrays.end();
}

std::size_t ShellState::array_length(std::string_view name) const {
  const auto it = arrays.find(std::string(name));
  return it == arrays.end() ? (is_set(name) ? 1 : 0) : it->second.size();
}

bool ShellState::set_assoc_element(std::string_view name, std::string_view key, std::string value) {
  assoc_arrays[std::string(name)][std::string(key)] = std::move(value);
  return true;
}

std::string ShellState::get_assoc_element(std::string_view name, std::string_view key) const {
  const auto it = assoc_arrays.find(std::string(name));
  if (it == assoc_arrays.end()) return {};
  const auto item = it->second.find(std::string(key));
  return item == it->second.end() ? std::string{} : item->second;
}

const std::unordered_map<std::string, std::string>* ShellState::get_assoc_array(std::string_view name) const {
  const auto it = assoc_arrays.find(std::string(name));
  return it == assoc_arrays.end() ? nullptr : &it->second;
}

bool ShellState::is_assoc(std::string_view name) const {
  return assoc_arrays.find(std::string(name)) != assoc_arrays.end();
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
