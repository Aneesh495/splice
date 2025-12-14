#pragma once

#include "history/history.hpp"
#include "expand/state.hpp"

#include <string>
#include <vector>

namespace splice::interactive {

class RawTerminal {
 public:
  explicit RawTerminal(int fd);
  ~RawTerminal();
  bool enable();
  void restore();
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

 private:
  int fd_{-1};
  bool enabled_{false};
  struct termios_state;
  termios_state* state_{nullptr};
};

class LineEditor {
 public:
  LineEditor(int input_fd, int output_fd, history::Store& history,
             const expand::ShellState* state = nullptr);
  ~LineEditor();

  [[nodiscard]] bool read_line(const std::string& prompt, std::string& line);
  void prepare_execute();
  void resume_input();
  void notify(const std::string& message);

 private:
  void render(const std::string& prompt, const std::string& buffer, std::size_t cursor);
  bool read_byte(char& value);
  bool read_escape(std::string& sequence);

  int input_fd_;
  int output_fd_;
  history::Store& history_;
  const expand::ShellState* state_{nullptr};
  RawTerminal terminal_;
  std::vector<history::Entry> entries_;
  std::size_t history_index_{0};
};

}  // namespace splice::interactive
