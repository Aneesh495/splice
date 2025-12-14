#include "interactive/editor.hpp"
#include "interactive/completion.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>

namespace splice::interactive {

struct RawTerminal::termios_state { struct termios value{}; };

RawTerminal::RawTerminal(int fd) : fd_(fd), state_(new termios_state) {}
RawTerminal::~RawTerminal() { restore(); delete state_; }

bool RawTerminal::enable() {
  if (enabled_ || fd_ < 0 || tcgetattr(fd_, &state_->value) != 0) return false;
  struct termios raw = state_->value;
  cfmakeraw(&raw);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(fd_, TCSANOW, &raw) != 0) return false;
  const std::string bracketed_paste = "\033[?2004h";
  ::write(STDOUT_FILENO, bracketed_paste.data(), bracketed_paste.size());
  enabled_ = true;
  return true;
}

void RawTerminal::restore() {
  if (!enabled_) return;
  tcsetattr(fd_, TCSANOW, &state_->value);
  const std::string bracketed_paste = "\033[?2004l";
  ::write(STDOUT_FILENO, bracketed_paste.data(), bracketed_paste.size());
  enabled_ = false;
}

LineEditor::LineEditor(int input_fd, int output_fd, history::Store& history,
                       const expand::ShellState* state)
    : input_fd_(input_fd), output_fd_(output_fd), history_(history), state_(state), terminal_(input_fd) {
  (void)history_.open();
  entries_ = history_.entries();
  history_index_ = entries_.size();
}

LineEditor::~LineEditor() { terminal_.restore(); }

void LineEditor::prepare_execute() { terminal_.restore(); }
void LineEditor::resume_input() { (void)terminal_.enable(); }

bool LineEditor::read_byte(char& value) {
  while (true) {
    const ssize_t count = ::read(input_fd_, &value, 1);
    if (count == 1) return true;
    if (count < 0 && errno == EINTR) continue;
    return false;
  }
}

bool LineEditor::read_escape(std::string& sequence) {
  sequence.clear();
  char value = '\0';
  if (!read_byte(value)) return false;
  sequence.push_back(value);
  if (value != '[' && value != 'O') return true;
  while (sequence.size() < 8) {
    if (!read_byte(value)) return false;
    sequence.push_back(value);
    if ((value >= '@' && value <= '~')) break;
  }
  return true;
}

void LineEditor::render(const std::string& prompt, const std::string& buffer, std::size_t cursor) {
  std::string output = "\r\033[2K" + prompt + buffer;
  output += "\033[" + std::to_string(buffer.size() - cursor) + "D";
  ::write(output_fd_, output.data(), output.size());
}

bool LineEditor::read_line(const std::string& prompt, std::string& line) {
  if (!terminal_.enabled() && !terminal_.enable()) {
    std::getline(std::cin, line);
    return static_cast<bool>(std::cin);
  }
  std::string buffer;
  std::size_t cursor = 0;
  entries_ = history_.entries();
  history_index_ = entries_.size();
  render(prompt, buffer, cursor);
  while (true) {
    char value = '\0';
    if (!read_byte(value)) return false;
    if (value == '\r' || value == '\n') {
      const std::string output = "\r\n";
      ::write(output_fd_, output.data(), output.size());
      line = buffer;
      return true;
    }
    if (value == 3) {
      const std::string output = "^C\r\n";
      ::write(output_fd_, output.data(), output.size());
      line.clear();
      return true;
    }
    if (value == 4) {
      if (buffer.empty()) {
        const std::string output = "\r\n";
        ::write(output_fd_, output.data(), output.size());
        return false;
      }
      buffer.erase(cursor, 1);
    } else if (value == '\t' && state_ != nullptr) {
      Completer completer(*state_);
      const auto comp_result = completer.complete(buffer, cursor);
      if (comp_result.is_unique()) {
        const auto& cand = comp_result.candidates.front();
        buffer.replace(comp_result.replacement_start, comp_result.replacement_length, cand.text);
        cursor = comp_result.replacement_start + cand.text.size();
        if (cand.kind != CompletionKind::Directory) {
          buffer.insert(cursor, " ");
          ++cursor;
        }
      } else if (!comp_result.empty()) {
        if (comp_result.common_prefix.size() > comp_result.replacement_length) {
          buffer.replace(comp_result.replacement_start, comp_result.replacement_length, comp_result.common_prefix);
          cursor = comp_result.replacement_start + comp_result.common_prefix.size();
        } else {
          std::string display = "\r\n";
          for (std::size_t i = 0; i < comp_result.candidates.size(); ++i) {
            if (i > 0) display += "  ";
            display += comp_result.candidates[i].display;
          }
          display += "\r\n";
          ::write(output_fd_, display.data(), display.size());
        }
      }
    } else if (value == 127 || value == 8) {
      if (cursor > 0) {
        buffer.erase(cursor - 1, 1);
        --cursor;
      }
    } else if (value == 1) {
      cursor = 0;
    } else if (value == 5) {
      cursor = buffer.size();
    } else if (value == 21) {
      buffer.erase(0, cursor);
      cursor = 0;
    } else if (value == 23) {
      while (cursor > 0 && buffer[cursor - 1] == ' ') { buffer.erase(--cursor, 1); }
      while (cursor > 0 && buffer[cursor - 1] != ' ') { buffer.erase(--cursor, 1); }
    } else if (value == 27) {
      std::string sequence;
      if (!read_escape(sequence)) return false;
      if (sequence == "[D" && cursor > 0) --cursor;
      else if (sequence == "[C" && cursor < buffer.size()) ++cursor;
      else if (sequence == "[H") cursor = 0;
      else if (sequence == "[F") cursor = buffer.size();
      else if (sequence == "[A" && history_index_ > 0) {
        --history_index_;
        buffer = entries_[history_index_].command;
        cursor = buffer.size();
      } else if (sequence == "[B") {
        if (history_index_ + 1 < entries_.size()) buffer = entries_[++history_index_].command;
        else { history_index_ = entries_.size(); buffer.clear(); }
        cursor = buffer.size();
      } else if (sequence == "[3~" && cursor < buffer.size()) buffer.erase(cursor, 1);
    } else if (value >= 32) {
      buffer.insert(cursor, 1, value);
      ++cursor;
    }
    render(prompt, buffer, cursor);
  }
}

void LineEditor::notify(const std::string& message) {
  const std::string output = "\r\033[2K" + message + "\n";
  ::write(output_fd_, output.data(), output.size());
}

}  // namespace splice::interactive
