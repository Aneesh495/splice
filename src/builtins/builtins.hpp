#pragma once

#include "expand/state.hpp"

#include <functional>
#include <string>
#include <vector>

namespace splice::builtins {

struct Context {
  expand::ShellState& state;
  int input_fd{0};
  int output_fd{1};
  int error_fd{2};
  bool parent{false};
  std::function<int(const std::vector<std::string>&)> nested;
  std::function<int(const std::vector<std::string>&)> job_control;
};

struct Result {
  bool recognized{false};
  int status{0};
  bool request_exit{false};
};

[[nodiscard]] bool is_builtin(std::string_view name) noexcept;
[[nodiscard]] Result run(const std::vector<std::string>& argv, Context& context);
[[nodiscard]] std::vector<std::string> names();

}  // namespace splice::builtins
