#include "history/history.hpp"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace splice::history {
namespace {

std::string escape(std::string_view value) {
  std::string result;
  for (char character : value) {
    if (character == '\\') result += "\\\\";
    else if (character == '\t') result += "\\t";
    else if (character == '\n') result += "\\n";
    else result.push_back(character);
  }
  return result;
}

std::string unescape(std::string_view value) {
  std::string result;
  bool escaped = false;
  for (char character : value) {
    if (!escaped) {
      if (character == '\\') escaped = true;
      else result.push_back(character);
      continue;
    }
    escaped = false;
    if (character == 'n') result.push_back('\n');
    else if (character == 't') result.push_back('\t');
    else result.push_back(character);
  }
  return result;
}

}  // namespace

Store::Store(std::string path, std::size_t retention)
    : path_(std::move(path)), retention_(retention) {}

std::string Store::encode(const Entry& entry) {
  return std::to_string(entry.timestamp) + '\t' + std::to_string(entry.status) + '\t' +
         escape(entry.directory) + '\t' + escape(entry.command) + '\n';
}

bool Store::decode(std::string_view record, Entry& entry) {
  const std::size_t first = record.find('\t');
  const std::size_t second = record.find('\t', first + 1);
  const std::size_t third = record.find('\t', second + 1);
  if (first == std::string_view::npos || second == std::string_view::npos || third == std::string_view::npos) return false;
  try {
    entry.timestamp = std::stoll(std::string(record.substr(0, first)));
    entry.status = std::stoi(std::string(record.substr(first + 1, second - first - 1)));
  } catch (...) {
    return false;
  }
  entry.directory = unescape(record.substr(second + 1, third - second - 1));
  entry.command = unescape(record.substr(third + 1));
  return true;
}

bool Store::load_locked() {
  if (loaded_) return true;
  entries_.clear();
  std::ifstream input(path_);
  if (input) {
    std::string line;
    while (std::getline(input, line)) {
      Entry entry;
      if (decode(line, entry)) entries_.push_back(std::move(entry));
    }
  }
  if (entries_.size() > retention_) entries_.erase(entries_.begin(), entries_.end() - static_cast<std::ptrdiff_t>(retention_));
  loaded_ = true;
  return true;
}

bool Store::open() {
  std::lock_guard lock(mutex_);
  return load_locked();
}

bool Store::append(Entry entry) {
  std::lock_guard lock(mutex_);
  if (!load_locked()) return false;
  if (entry.timestamp == 0) {
    entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
  }
  std::error_code error;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, error);
  std::ofstream output(path_, std::ios::app);
  if (!output) return false;
  output << encode(entry);
  if (!output) return false;
  entries_.push_back(std::move(entry));
  if (entries_.size() > retention_) return rewrite_locked();
  return true;
}

bool Store::rewrite_locked() {
  const std::string temporary = path_ + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output) return false;
  for (const auto& entry : entries_) output << encode(entry);
  output.close();
  if (!output) return false;
  std::error_code error;
  std::filesystem::rename(temporary, path_, error);
  return !error;
}

std::vector<Entry> Store::entries() const {
  std::lock_guard lock(mutex_);
  const_cast<Store*>(this)->load_locked();
  return entries_;
}

std::vector<Entry> Store::search(std::string_view needle) const {
  std::vector<Entry> result;
  for (const auto& entry : entries()) if (entry.command.find(needle) != std::string::npos) result.push_back(entry);
  return result;
}

void Store::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
  loaded_ = true;
  std::error_code error;
  std::filesystem::remove(path_, error);
}

}  // namespace splice::history
