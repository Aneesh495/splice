#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace splice::source {

struct Span {
  std::size_t begin{0};
  std::size_t end{0};

  [[nodiscard]] constexpr bool empty() const noexcept { return begin == end; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return end - begin; }
};

class SourceBuffer {
 public:
  SourceBuffer() = default;
  explicit SourceBuffer(std::string text, std::string name = "<stdin>")
      : text_(std::move(text)), name_(std::move(name)) {}

  [[nodiscard]] const std::string& text() const noexcept { return text_; }
  [[nodiscard]] std::string_view slice(Span span) const noexcept {
    if (span.begin > span.end || span.end > text_.size()) return {};
    return std::string_view(text_).substr(span.begin, span.size());
  }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  struct Position {
    std::size_t line{1};
    std::size_t column{1};
  };

  [[nodiscard]] Position position(std::size_t offset) const noexcept {
    if (offset > text_.size()) offset = text_.size();
    std::size_t line = 1;
    std::size_t line_start = 0;
    for (std::size_t index = 0; index < offset; ++index) {
      if (text_[index] == '\n') {
        ++line;
        line_start = index + 1;
      }
    }
    return Position{line, offset - line_start + 1};
  }

 private:
  std::string text_;
  std::string name_{"<stdin>"};
};

enum class Severity { Note, Warning, Error };

struct Diagnostic {
  Severity severity{Severity::Error};
  Span span{};
  std::string message;
  std::string repair;

  [[nodiscard]] std::string label() const {
    switch (severity) {
      case Severity::Note: return "note";
      case Severity::Warning: return "warning";
      case Severity::Error: return "error";
    }
    return "error";
  }
};

struct Diagnostics {
  std::vector<Diagnostic> entries;

  void error(Span span, std::string message, std::string repair = {}) {
    entries.push_back(Diagnostic{Severity::Error, span, std::move(message), std::move(repair)});
  }
  void warning(Span span, std::string message, std::string repair = {}) {
    entries.push_back(Diagnostic{Severity::Warning, span, std::move(message), std::move(repair)});
  }
  [[nodiscard]] bool has_error() const noexcept {
    for (const auto& entry : entries) {
      if (entry.severity == Severity::Error) return true;
    }
    return false;
  }
};

}  // namespace splice::source
