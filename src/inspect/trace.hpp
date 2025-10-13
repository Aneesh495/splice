#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace splice::inspect {

struct TraceEvent {
  std::uint64_t sequence{0};
  std::string kind;
  std::uint64_t monotonic_ns{0};
  std::map<std::string, std::string> fields;
};

class Trace {
 public:
  explicit Trace(std::string path = {});
  ~Trace();

  bool open();
  void close();
  [[nodiscard]] std::uint64_t record(std::string kind,
                                      std::map<std::string, std::string> fields = {});
  [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
  [[nodiscard]] const std::string& path() const noexcept { return path_; }

 private:
  static std::string json_escape(std::string_view value);
  std::string path_;
  std::ofstream output_;
  mutable std::mutex mutex_;
  std::uint64_t sequence_{0};
};

}  // namespace splice::inspect
