#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace splice::history {

struct Entry {
  std::string command;
  long long timestamp{0};
  int status{0};
  std::string directory;
};

class Store {
 public:
  explicit Store(std::string path, std::size_t retention = 10000);

  [[nodiscard]] bool open();
  [[nodiscard]] bool append(Entry entry);
  [[nodiscard]] std::vector<Entry> entries() const;
  [[nodiscard]] std::vector<Entry> search(std::string_view needle) const;
  [[nodiscard]] std::string path() const { return path_; }
  void clear();

 private:
  bool load_locked();
  bool rewrite_locked();
  static std::string encode(const Entry& entry);
  static bool decode(std::string_view record, Entry& entry);

  std::string path_;
  std::size_t retention_{10000};
  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
  bool loaded_{false};
};

}  // namespace splice::history
