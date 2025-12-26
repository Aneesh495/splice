#include "expand/glob.hpp"

#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <set>

namespace splice::expand {
namespace {

std::vector<std::string> split_path_segments(std::string_view path) {
  std::vector<std::string> segments;
  std::size_t start = 0;
  while (start < path.size()) {
    while (start < path.size() && path[start] == '/') ++start;
    if (start >= path.size()) break;
    const std::size_t end = path.find('/', start);
    if (end == std::string_view::npos) {
      segments.emplace_back(path.substr(start));
      break;
    }
    segments.emplace_back(path.substr(start, end - start));
    start = end + 1;
  }
  return segments;
}

bool is_directory(const std::string& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

std::string join_paths(std::string_view base, std::string_view child) {
  if (base.empty() || base == ".") return std::string(child);
  if (base == "/") return "/" + std::string(child);
  if (base.back() == '/') return std::string(base) + std::string(child);
  return std::string(base) + "/" + std::string(child);
}

std::string to_lower_str(std::string_view s) {
  std::string lower(s);
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower;
}

}  // namespace

void PathGlobber::collect_globstar(std::string_view dir_path,
                                   const std::vector<std::string>& segments,
                                   std::size_t segment_index,
                                   std::vector<std::string>& results,
                                   std::size_t depth) const {
  if (depth > options_.max_depth) return;

  // 1. Zero-directory match: skip ** and proceed directly with remaining segments
  expand_recursive(dir_path, segments, segment_index + 1, results, depth + 1);

  // 2. Open directory and traverse subdirectories
  const std::string dir_str = dir_path.empty() ? "." : std::string(dir_path);
  DIR* dp = opendir(dir_str.c_str());
  if (dp == nullptr) return;

  struct dirent* entry = nullptr;
  std::vector<std::string> subdirs;
  while ((entry = readdir(dp)) != nullptr) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") continue;
    if (!options_.dotglob && name.starts_with(".")) continue;

    const std::string sub_path = join_paths(dir_path, name);
    if (is_directory(sub_path)) {
      subdirs.push_back(sub_path);
    }
  }
  closedir(dp);

  std::sort(subdirs.begin(), subdirs.end());
  for (const auto& subdir : subdirs) {
    // Continue globstar traversal in subdirectory
    collect_globstar(subdir, segments, segment_index, results, depth + 1);
  }
}

void PathGlobber::traverse_directory(std::string_view dir_path,
                                    const std::vector<std::string>& segments,
                                    std::size_t segment_index,
                                    std::vector<std::string>& results,
                                    std::size_t depth) const {
  if (depth > options_.max_depth) return;
  const std::string& pattern_segment = segments[segment_index];
  const bool is_last = (segment_index + 1 == segments.size());

  const std::string dir_str = dir_path.empty() ? "." : std::string(dir_path);
  DIR* dp = opendir(dir_str.c_str());
  if (dp == nullptr) return;

  Pattern pat(options_.nocaseglob ? to_lower_str(pattern_segment) : pattern_segment, true);
  struct dirent* entry = nullptr;
  std::vector<std::string> matches;

  while ((entry = readdir(dp)) != nullptr) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") continue;
    if (!options_.dotglob && !pattern_segment.starts_with(".") && name.starts_with(".")) {
      continue;
    }

    const std::string test_name = options_.nocaseglob ? to_lower_str(name) : std::string(name);
    if (pat.matches(test_name)) {
      matches.emplace_back(name);
    }
  }
  closedir(dp);

  std::sort(matches.begin(), matches.end());
  for (const auto& match : matches) {
    const std::string child_path = join_paths(dir_path, match);
    if (is_last) {
      results.push_back(child_path);
    } else if (is_directory(child_path)) {
      expand_recursive(child_path, segments, segment_index + 1, results, depth + 1);
    }
  }
}

void PathGlobber::expand_recursive(std::string_view current_dir,
                                  const std::vector<std::string>& segments,
                                  std::size_t segment_index,
                                  std::vector<std::string>& results,
                                  std::size_t depth) const {
  if (depth > options_.max_depth) return;
  if (segment_index >= segments.size()) {
    if (!current_dir.empty()) results.emplace_back(current_dir);
    return;
  }

  const std::string& segment = segments[segment_index];
  if (options_.globstar && segment == "**") {
    collect_globstar(current_dir, segments, segment_index, results, depth);
    return;
  }

  if (!Pattern::is_pattern(segment)) {
    const std::string next_path = join_paths(current_dir, segment);
    if (segment_index + 1 == segments.size()) {
      struct stat st{};
      if (stat(next_path.c_str(), &st) == 0) {
        results.push_back(next_path);
      }
    } else if (is_directory(next_path)) {
      expand_recursive(next_path, segments, segment_index + 1, results, depth + 1);
    }
    return;
  }

  traverse_directory(current_dir, segments, segment_index, results, depth);
}

GlobResult PathGlobber::expand(std::string_view pattern_str) const {
  GlobResult result;
  if (pattern_str.empty()) {
    result.matches.emplace_back();
    return result;
  }

  const bool is_absolute = pattern_str.front() == '/';
  const auto segments = split_path_segments(pattern_str);
  if (segments.empty()) {
    if (is_absolute) result.matches.push_back("/");
    return result;
  }

  const std::string base_dir = is_absolute ? "/" : "";
  expand_recursive(base_dir, segments, 0, result.matches, 0);

  // If pattern was directory-ended (e.g. `dir/*/`), ensure only directories remain
  if (pattern_str.back() == '/') {
    std::vector<std::string> filtered;
    for (auto& match : result.matches) {
      if (is_directory(match)) {
        if (!match.ends_with('/')) match.push_back('/');
        filtered.push_back(std::move(match));
      }
    }
    result.matches = std::move(filtered);
  }

  if (result.matches.empty()) {
    if (options_.nullglob) {
      return result;
    }
    if (options_.failglob) {
      result.error = true;
      result.error_message = "no matches found: " + std::string(pattern_str);
      return result;
    }
    result.matches.push_back(std::string(pattern_str));
  } else {
    std::sort(result.matches.begin(), result.matches.end());
    result.matches.erase(std::unique(result.matches.begin(), result.matches.end()), result.matches.end());
  }

  return result;
}

std::vector<std::string> expand_glob(std::string_view pattern_str, GlobOptions options) {
  PathGlobber globber(options);
  auto res = globber.expand(pattern_str);
  return res.matches;
}

}  // namespace splice::expand
