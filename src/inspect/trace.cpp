#include "inspect/trace.hpp"

#include <chrono>
#include <sstream>

namespace splice::inspect {

Trace::Trace(std::string path) : path_(std::move(path)) {}
Trace::~Trace() { close(); }

bool Trace::open() {
  std::lock_guard lock(mutex_);
  if (path_.empty()) return false;
  output_.open(path_, std::ios::app);
  return static_cast<bool>(output_);
}

void Trace::close() {
  std::lock_guard lock(mutex_);
  if (output_.is_open()) output_.close();
}

std::string Trace::json_escape(std::string_view value) {
  std::string result;
  for (char character : value) {
    if (character == '"') result += "\\\"";
    else if (character == '\\') result += "\\\\";
    else if (character == '\n') result += "\\n";
    else if (character == '\r') result += "\\r";
    else result.push_back(character);
  }
  return result;
}

std::uint64_t Trace::record(std::string kind, std::map<std::string, std::string> fields) {
  std::lock_guard lock(mutex_);
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto monotonic = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  const std::uint64_t sequence = ++sequence_;
  if (!output_.is_open()) return sequence;
  output_ << "{\"schema\":1,\"sequence\":" << sequence << ",\"kind\":\""
          << json_escape(kind) << "\",\"monotonic_ns\":" << monotonic << ",\"fields\":{";
  bool first = true;
  for (const auto& [name, value] : fields) {
    if (!first) output_ << ',';
    first = false;
    output_ << '"' << json_escape(name) << "\":\"" << json_escape(value) << '"';
  }
  output_ << "}}\n";
  output_.flush();
  return sequence;
}

}  // namespace splice::inspect
