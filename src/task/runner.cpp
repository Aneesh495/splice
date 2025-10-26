#include "task/runner.hpp"

#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <variant>

extern char** environ;

namespace splice::task {
namespace {

struct Json {
  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;
  std::variant<std::nullptr_t, bool, double, std::string, Object, Array> value{nullptr};
  [[nodiscard]] const Json* get(std::string_view key) const {
    const auto* object = std::get_if<Object>(&value);
    if (object == nullptr) return nullptr;
    const auto iterator = object->find(std::string(key));
    return iterator == object->end() ? nullptr : &iterator->second;
  }
};

class JsonParser {
 public:
  explicit JsonParser(std::string text) : text_(std::move(text)) {}
  Json parse() {
    skip();
    Json value = parse_value();
    skip();
    if (index_ != text_.size()) fail("trailing JSON bytes");
    return value;
  }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }

 private:
  void fail(std::string message) {
    if (error_.empty()) error_ = std::move(message);
  }
  void skip() { while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_])) != 0) ++index_; }
  bool take(char expected) {
    skip();
    if (index_ >= text_.size() || text_[index_] != expected) { fail("expected JSON character"); return false; }
    ++index_;
    return true;
  }
  Json parse_value() {
    skip();
    if (index_ >= text_.size()) { fail("unexpected JSON end"); return {}; }
    if (text_[index_] == '"') return Json{parse_string()};
    if (text_[index_] == '{') return Json{parse_object()};
    if (text_[index_] == '[') return Json{parse_array()};
    if (text_.compare(index_, 4, "true") == 0) { index_ += 4; return Json{true}; }
    if (text_.compare(index_, 5, "false") == 0) { index_ += 5; return Json{false}; }
    if (text_.compare(index_, 4, "null") == 0) { index_ += 4; return Json{nullptr}; }
    const std::size_t start = index_;
    while (index_ < text_.size() && std::string_view("-+0123456789.eE").find(text_[index_]) != std::string_view::npos) ++index_;
    try { return Json{std::stod(text_.substr(start, index_ - start))}; }
    catch (...) { fail("invalid JSON value"); return {}; }
  }
  std::string parse_string() {
    if (!take('"')) return {};
    std::string result;
    while (index_ < text_.size()) {
      char value = text_[index_++];
      if (value == '"') return result;
      if (value != '\\') { result.push_back(value); continue; }
      if (index_ >= text_.size()) break;
      value = text_[index_++];
      if (value == 'n') result.push_back('\n');
      else if (value == 'r') result.push_back('\r');
      else if (value == 't') result.push_back('\t');
      else result.push_back(value);
    }
    fail("unterminated JSON string");
    return result;
  }
  Json::Object parse_object() {
    Json::Object result;
    if (!take('{')) return result;
    skip();
    if (index_ < text_.size() && text_[index_] == '}') { ++index_; return result; }
    while (index_ < text_.size()) {
      const std::string key = parse_string();
      if (!take(':')) return result;
      result.emplace(key, parse_value());
      skip();
      if (index_ < text_.size() && text_[index_] == '}') { ++index_; return result; }
      if (!take(',')) return result;
    }
    fail("unterminated JSON object");
    return result;
  }
  Json::Array parse_array() {
    Json::Array result;
    if (!take('[')) return result;
    skip();
    if (index_ < text_.size() && text_[index_] == ']') { ++index_; return result; }
    while (index_ < text_.size()) {
      result.push_back(parse_value());
      skip();
      if (index_ < text_.size() && text_[index_] == ']') { ++index_; return result; }
      if (!take(',')) return result;
    }
    fail("unterminated JSON array");
    return result;
  }

  std::string text_;
  std::size_t index_{0};
  std::string error_;
};

bool string_value(const Json* value, std::string& output) {
  if (value == nullptr) return false;
  const auto* string = std::get_if<std::string>(&value->value);
  if (string == nullptr) return false;
  output = *string;
  return true;
}

bool integer_value(const Json* value, int& output) {
  if (value == nullptr) return false;
  const auto* number = std::get_if<double>(&value->value);
  if (number == nullptr) return false;
  output = static_cast<int>(*number);
  return true;
}

std::string escape_json(std::string_view value) {
  std::string result;
  for (char character : value) {
    if (character == '"') result += "\\\"";
    else if (character == '\\') result += "\\\\";
    else if (character == '\n') result += "\\n";
    else result.push_back(character);
  }
  return result;
}

struct Active {
  TaskSpec spec;
  pid_t pid{-1};
  pid_t process_group{-1};
  int stdout_fd{-1};
  int stderr_fd{-1};
  std::string stdout_text;
  std::string stderr_text;
  std::chrono::steady_clock::time_point started;
  bool timed_out{false};
  std::chrono::steady_clock::time_point terminated_at{};
};

std::string resolve_executable(std::string_view name) {
  if (name.find('/') != std::string_view::npos) return std::string(name);
  const char* path_value = std::getenv("PATH");
  const std::string path = path_value == nullptr ? std::string{} : std::string(path_value);
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t separator = path.find(':', start);
    const std::string directory = path.substr(start, separator == std::string::npos ? std::string::npos : separator - start);
    const std::string candidate = (directory.empty() ? "." : directory) + "/" + std::string(name);
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
    if (separator == std::string::npos) break;
    start = separator + 1;
  }
  return {};
}

void set_nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void drain_fd(int& fd, std::string& output) {
  if (fd < 0) return;
  char buffer[4096];
  while (true) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      constexpr std::size_t limit = 1024 * 1024;
      const std::size_t available = output.size() < limit ? limit - output.size() : 0;
      output.append(buffer, std::min<std::size_t>(available, static_cast<std::size_t>(count)));
      continue;
    }
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return;
    close(fd);
    fd = -1;
    return;
  }
}

}  // namespace

bool Runner::load(std::string path, std::string& error) {
  tasks_.clear();
  manifest_path_ = std::move(path);
  std::ifstream input(manifest_path_);
  if (!input) { error = "cannot open manifest"; return false; }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  JsonParser parser(buffer.str());
  const Json root = parser.parse();
  if (!parser.error().empty()) { error = parser.error(); return false; }
  const Json* task_array = root.get("tasks");
  if (task_array == nullptr) { error = "manifest requires tasks array"; return false; }
  const auto* values = std::get_if<Json::Array>(&task_array->value);
  if (values == nullptr || values->empty()) { error = "tasks must be a non-empty array"; return false; }
  for (const auto& value : *values) {
    const auto* object = std::get_if<Json::Object>(&value.value);
    if (object == nullptr) { error = "each task must be an object"; return false; }
    TaskSpec task;
    if (!string_value(value.get("id"), task.id) || task.id.empty()) { error = "task id is required"; return false; }
    const Json* argv = value.get("argv");
    const auto* arguments = argv == nullptr ? nullptr : std::get_if<Json::Array>(&argv->value);
    if (arguments == nullptr || arguments->empty()) { error = "task argv must be non-empty"; return false; }
    for (const auto& argument : *arguments) {
      std::string text;
      if (!string_value(&argument, text)) { error = "task argv values must be strings"; return false; }
      task.argv.push_back(std::move(text));
    }
    (void)string_value(value.get("cwd"), task.cwd);
    (void)string_value(value.get("output"), task.output);
    (void)integer_value(value.get("timeout_ms"), task.timeout_ms);
    if (task.output != "capture" && task.output != "inherit" && task.output != "discard") { error = "unsupported task output policy"; return false; }
    if (const auto* environment = value.get("env"); environment != nullptr) {
      const auto* object_env = std::get_if<Json::Object>(&environment->value);
      if (object_env == nullptr) { error = "task env must be an object"; return false; }
      for (const auto& [name, env_value] : *object_env) {
        std::string text;
        if (!string_value(&env_value, text)) { error = "task env values must be strings"; return false; }
        task.environment.emplace_back(name, std::move(text));
      }
    }
    tasks_.push_back(std::move(task));
  }
  return true;
}

std::vector<TaskOutcome> Runner::run(std::string& error) {
  std::vector<TaskOutcome> outcomes;
  std::vector<Active> active;
  std::size_t next = 0;
  auto launch = [&](const TaskSpec& task) -> bool {
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    if (task.output == "capture") {
      if (pipe(stdout_pipe) != 0) return false;
      if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
      }
    }
    const pid_t pid = fork();
    if (pid < 0) {
      close(stdout_pipe[0]); close(stdout_pipe[1]); close(stderr_pipe[0]); close(stderr_pipe[1]);
      return false;
    }
    if (pid == 0) {
      setpgid(0, 0);
      if (!task.cwd.empty() && chdir(task.cwd.c_str()) != 0) _exit(125);
      for (const auto& [name, value] : task.environment) if (setenv(name.c_str(), value.c_str(), 1) != 0) _exit(125);
      if (task.output == "capture") {
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 || dup2(stderr_pipe[1], STDERR_FILENO) < 0) _exit(125);
      } else if (task.output == "discard") {
        const int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0 || dup2(null_fd, STDERR_FILENO) < 0) _exit(125);
        if (null_fd > STDERR_FILENO) close(null_fd);
      }
      close(stdout_pipe[0]); close(stdout_pipe[1]); close(stderr_pipe[0]); close(stderr_pipe[1]);
      std::vector<char*> arguments;
      for (const auto& argument : task.argv) arguments.push_back(const_cast<char*>(argument.c_str()));
      arguments.push_back(nullptr);
      const std::string executable = resolve_executable(task.argv.front());
      if (executable.empty()) _exit(127);
      execve(executable.c_str(), arguments.data(), environ);
      _exit(errno == EACCES ? 126 : 127);
    }
    setpgid(pid, pid);
    if (task.output == "capture") {
      close(stdout_pipe[1]); close(stderr_pipe[1]);
      set_nonblocking(stdout_pipe[0]); set_nonblocking(stderr_pipe[0]);
    }
    active.push_back(Active{task, pid, pid, stdout_pipe[0], stderr_pipe[0], {}, {}, std::chrono::steady_clock::now(), false, {}});
    return true;
  };
  auto cleanup_active = [&]() {
    for (auto& item : active) {
      if (item.process_group > 0) kill(-item.process_group, SIGTERM);
    }
    usleep(100000);
    for (auto& item : active) {
      if (item.process_group > 0) kill(-item.process_group, SIGKILL);
    }
    for (auto& item : active) {
      int status = 0;
      while (waitpid(item.pid, &status, 0) < 0 && errno == EINTR) {}
      drain_fd(item.stdout_fd, item.stdout_text);
      drain_fd(item.stderr_fd, item.stderr_text);
      if (item.stdout_fd >= 0) close(item.stdout_fd);
      if (item.stderr_fd >= 0) close(item.stderr_fd);
    }
  };
  while (next < tasks_.size() || !active.empty()) {
    while (next < tasks_.size() && active.size() < max_parallel_) {
      if (!launch(tasks_[next])) {
        cleanup_active();
        error = "task launch failed";
        return outcomes;
      }
      ++next;
    }
    std::vector<struct pollfd> descriptors;
    for (const auto& item : active) {
      if (item.stdout_fd >= 0) descriptors.push_back(pollfd{item.stdout_fd, POLLIN, 0});
      if (item.stderr_fd >= 0) descriptors.push_back(pollfd{item.stderr_fd, POLLIN, 0});
    }
    (void)poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), 20);
    for (auto& item : active) {
      drain_fd(item.stdout_fd, item.stdout_text);
      drain_fd(item.stderr_fd, item.stderr_text);
      const auto now = std::chrono::steady_clock::now();
      if (item.spec.timeout_ms > 0 && !item.timed_out &&
          std::chrono::duration_cast<std::chrono::milliseconds>(now - item.started).count() > item.spec.timeout_ms) {
        if (item.process_group > 0) kill(-item.process_group, SIGTERM);
        item.timed_out = true;
        item.terminated_at = now;
      } else if (item.timed_out && item.process_group > 0 &&
                 std::chrono::duration_cast<std::chrono::milliseconds>(now - item.terminated_at).count() > 100) {
        kill(-item.process_group, SIGKILL);
      }
    }
    for (std::size_t index = 0; index < active.size();) {
      int status = 0;
      const pid_t waited = waitpid(active[index].pid, &status, WNOHANG);
      if (waited == 0) { ++index; continue; }
      if (waited < 0 && errno == EINTR) continue;
      if (waited < 0) {
        if (active[index].stdout_fd >= 0) close(active[index].stdout_fd);
        if (active[index].stderr_fd >= 0) close(active[index].stderr_fd);
        outcomes.push_back(TaskOutcome{active[index].spec.id, 125, active[index].timed_out, 0, std::move(active[index].stdout_text), std::move(active[index].stderr_text)});
        active.erase(active.begin() + static_cast<std::ptrdiff_t>(index));
        continue;
      }
      drain_fd(active[index].stdout_fd, active[index].stdout_text);
      drain_fd(active[index].stderr_fd, active[index].stderr_text);
      if (active[index].stdout_fd >= 0) close(active[index].stdout_fd);
      if (active[index].stderr_fd >= 0) close(active[index].stderr_fd);
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - active[index].started).count();
      const int result_status = active[index].timed_out ? 124 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
      outcomes.push_back(TaskOutcome{active[index].spec.id, result_status, active[index].timed_out, duration, std::move(active[index].stdout_text), std::move(active[index].stderr_text)});
      active.erase(active.begin() + static_cast<std::ptrdiff_t>(index));
    }
  }
  return outcomes;
}

std::string Runner::dump_json(const std::vector<TaskOutcome>& outcomes) const {
  std::ostringstream output;
  output << "{\"schema\":1,\"outcomes\":[";
  for (std::size_t index = 0; index < outcomes.size(); ++index) {
    if (index != 0) output << ',';
    const auto& item = outcomes[index];
    output << "{\"id\":\"" << escape_json(item.id) << "\",\"status\":" << item.status
           << ",\"timed_out\":" << (item.timed_out ? "true" : "false")
           << ",\"duration_ms\":" << item.duration_ms << ",\"stdout\":\""
           << escape_json(item.stdout_text) << "\",\"stderr\":\"" << escape_json(item.stderr_text) << "\"}";
  }
  output << "]}";
  return output.str();
}

}  // namespace splice::task
