#pragma once

#include "expand/state.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace splice::interactive {

enum class CompletionKind {
  Command,
  File,
  Directory,
  Variable,
  Option,
  Builtin
};

struct Candidate {
  std::string text;
  std::string display;
  CompletionKind kind{CompletionKind::File};
  std::string description;
};

struct CompletionResult {
  std::vector<Candidate> candidates;
  std::size_t replacement_start{0};
  std::size_t replacement_length{0};
  std::string common_prefix;

  [[nodiscard]] bool empty() const noexcept { return candidates.empty(); }
  [[nodiscard]] bool is_unique() const noexcept { return candidates.size() == 1; }
};

class Completer {
 public:
  explicit Completer(const expand::ShellState& state);

  [[nodiscard]] CompletionResult complete(std::string_view line, std::size_t cursor) const;

 private:
  [[nodiscard]] std::vector<Candidate> complete_commands(std::string_view prefix) const;
  [[nodiscard]] std::vector<Candidate> complete_files(std::string_view prefix, bool directories_only = false) const;
  [[nodiscard]] std::vector<Candidate> complete_variables(std::string_view prefix) const;
  [[nodiscard]] std::string longest_common_prefix(const std::vector<Candidate>& candidates) const;

  const expand::ShellState& state_;
};

}  // namespace splice::interactive
