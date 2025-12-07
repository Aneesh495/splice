#pragma once

#include "syntax/ast.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace splice::expand {

enum class Profile { Posix, Bash, Splice };

enum class ShellOption { Errexit, Nounset, Noclobber, Pipefail };

struct Variable {
  std::string value;
  bool exported{false};
  bool readonly{false};
};

class ShellState {
 public:
  ShellState();

  [[nodiscard]] const Variable* find(std::string_view name) const;
  [[nodiscard]] std::string value(std::string_view name) const;
  [[nodiscard]] bool is_set(std::string_view name) const;
  [[nodiscard]] bool is_exported(std::string_view name) const;
  [[nodiscard]] bool is_readonly(std::string_view name) const;
  bool set(std::string name, std::string value, bool exported = false);
  bool unset(std::string_view name);
  bool mark_exported(std::string_view name, bool exported = true);
  bool mark_readonly(std::string_view name);
  [[nodiscard]] std::vector<std::string> environment() const;

  void set_profile(Profile profile) noexcept { profile_ = profile; }
  [[nodiscard]] Profile profile() const noexcept { return profile_; }
  bool set_option(ShellOption option, bool enabled = true);
  [[nodiscard]] bool option(ShellOption option) const noexcept;

  struct FunctionFrame {
    std::unordered_map<std::string, Variable> local_vars;
    std::vector<std::string> positional;
  };

  void push_function_frame(std::vector<std::string> new_positional);
  void pop_function_frame();
  bool set_local(std::string name, std::string value);

  bool set_array_element(std::string_view name, std::size_t index, std::string value);
  [[nodiscard]] std::string get_array_element(std::string_view name, std::size_t index) const;
  bool set_array(std::string name, std::vector<std::string> values);
  [[nodiscard]] const std::vector<std::string>* get_array(std::string_view name) const;
  [[nodiscard]] bool is_array(std::string_view name) const;
  [[nodiscard]] std::size_t array_length(std::string_view name) const;

  bool set_assoc_element(std::string_view name, std::string_view key, std::string value);
  [[nodiscard]] std::string get_assoc_element(std::string_view name, std::string_view key) const;
  [[nodiscard]] const std::unordered_map<std::string, std::string>* get_assoc_array(std::string_view name) const;
  [[nodiscard]] bool is_assoc(std::string_view name) const;

  std::unordered_map<std::string, syntax::CommandPtr> functions;
  std::vector<std::string> positional;
  std::vector<FunctionFrame> function_frames;
  std::unordered_map<std::string, std::vector<std::string>> arrays;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> assoc_arrays;
  std::unordered_map<std::string, std::string> aliases;
  std::vector<std::string> directory_stack;
  std::unordered_map<int, std::string> traps;
  std::vector<int> pipe_status;

  int last_status{0};
  long long last_background_pid{0};
  long long shell_pid{0};
  unsigned long command_number{0};
  std::string last_argument;
  std::string current_directory;

  bool return_requested{false};
  int return_status{0};
  int break_levels{0};
  int continue_levels{0};

 private:
  std::unordered_map<std::string, Variable> variables_;
  std::unordered_set<ShellOption> options_;
  Profile profile_{Profile::Posix};
};

[[nodiscard]] const char* profile_name(Profile profile) noexcept;
[[nodiscard]] bool parse_profile(std::string_view text, Profile& profile) noexcept;
[[nodiscard]] bool valid_name(std::string_view name) noexcept;

}  // namespace splice::expand
