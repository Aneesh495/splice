#pragma once

#include "expand/pattern.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace splice::expand {

struct GlobOptions {
  bool globstar{true};
  bool dotglob{false};
  bool nullglob{false};
  bool failglob{false};
  bool nocaseglob{false};
  std::size_t max_depth{64};
};

struct GlobResult {
  std::vector<std::string> matches;
  bool error{false};
  std::string error_message;
};

class PathGlobber {
 public:
  explicit PathGlobber(GlobOptions options = {}) : options_(options) {}

  [[nodiscard]] GlobResult expand(std::string_view pattern_str) const;

 private:
  void expand_recursive(std::string_view current_dir,
                        const std::vector<std::string>& segments,
                        std::size_t segment_index,
                        std::vector<std::string>& results,
                        std::size_t depth) const;

  void traverse_directory(std::string_view dir_path,
                          const std::vector<std::string>& segments,
                          std::size_t segment_index,
                          std::vector<std::string>& results,
                          std::size_t depth) const;

  void collect_globstar(std::string_view dir_path,
                        const std::vector<std::string>& segments,
                        std::size_t segment_index,
                        std::vector<std::string>& results,
                        std::size_t depth) const;

  GlobOptions options_;
};

std::vector<std::string> expand_glob(std::string_view pattern_str, GlobOptions options = {});

}  // namespace splice::expand
