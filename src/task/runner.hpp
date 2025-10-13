#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace splice::task {

struct TaskSpec {
  std::string id;
  std::vector<std::string> argv;
  std::string cwd;
  std::vector<std::pair<std::string, std::string>> environment;
  int timeout_ms{0};
  std::string output{"capture"};
};

struct TaskOutcome {
  std::string id;
  int status{125};
  bool timed_out{false};
  long long duration_ms{0};
  std::string stdout_text;
  std::string stderr_text;
};

class Runner {
 public:
  explicit Runner(std::size_t max_parallel = 1) : max_parallel_(max_parallel == 0 ? 1 : max_parallel) {}

  [[nodiscard]] bool load(std::string path, std::string& error);
  [[nodiscard]] std::vector<TaskOutcome> run(std::string& error);
  [[nodiscard]] std::string dump_json(const std::vector<TaskOutcome>& outcomes) const;
  [[nodiscard]] const std::vector<TaskSpec>& tasks() const noexcept { return tasks_; }

 private:
  std::size_t max_parallel_{1};
  std::string manifest_path_;
  std::vector<TaskSpec> tasks_;
};

}  // namespace splice::task
