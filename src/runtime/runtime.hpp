#pragma once

#include "builtins/builtins.hpp"
#include "plan/plan.hpp"
#include "source/source.hpp"
#include "inspect/trace.hpp"

#include <map>
#include <signal.h>
#include <string>
#include <vector>
#include <sys/types.h>

namespace splice::runtime {

struct ProcessId {
  pid_t value{-1};
  friend bool operator<(ProcessId left, ProcessId right) noexcept { return left.value < right.value; }
};

struct ProcessResult {
  ProcessId pid;
  int status{0};
  bool exited{false};
  bool signaled{false};
};

enum class JobState { Running, Stopped, Completed };

struct Job {
  int id{0};
  pid_t process_group{-1};
  std::vector<ProcessId> processes;
  std::vector<ProcessResult> results;
  JobState state{JobState::Running};
  bool background{false};
  int status{0};
};

class Runtime {
 public:
  explicit Runtime(expand::ShellState& state, inspect::Trace* trace = nullptr);
  ~Runtime();

  [[nodiscard]] int execute(const syntax::Program& program);
  [[nodiscard]] int execute_command(const syntax::CommandPtr& command);
  [[nodiscard]] std::string substitute(const std::string& source, int& status);
  void reap_nonblocking();
  [[nodiscard]] const std::map<int, Job>& jobs() const noexcept { return jobs_; }
  [[nodiscard]] bool exit_requested() const noexcept { return exit_requested_; }

 private:
  [[nodiscard]] int execute_sequence(const syntax::CommandPtr& command);
  [[nodiscard]] int execute_and_or(const syntax::CommandPtr& command);
  [[nodiscard]] int execute_plan(const plan::ExecutionPlan& plan);
  [[nodiscard]] int execute_parent_builtin(const plan::PlannedCommand& command);
  [[nodiscard]] int launch_pipeline(const plan::ExecutionPlan& plan);
  [[nodiscard]] int child_setup_and_exec(const plan::PlannedCommand& command,
                                         int input_fd, int output_fd,
                                         int error_fd, int error_pipe);
  [[nodiscard]] int apply_descriptor_actions(const plan::PlannedCommand& command);
  [[nodiscard]] int apply_parent_descriptors(const plan::PlannedCommand& command,
                                             std::vector<int>& saved);
  void restore_parent_descriptors(const std::vector<int>& saved);
  [[nodiscard]] std::vector<std::string> environment_for(const plan::PlannedCommand& command) const;
  [[nodiscard]] std::string resolve_executable(const std::string& name) const;
  [[nodiscard]] int wait_foreground(Job& job);
  [[nodiscard]] int job_control(const std::vector<std::string>& argv);
  [[nodiscard]] int decode_status(int status) const noexcept;
  [[nodiscard]] Job* find_job_for_pid(pid_t pid);
  void register_job(Job job);
  void reap_one(pid_t pid, int status);
  void report_error(std::string_view message) const;
  void install_signal_bridge();
  void restore_signal_bridge();
  void drain_signal_bridge();
  static void signal_handler(int signal_number) noexcept;

  expand::ShellState& state_;
  expand::Expander expander_;
  plan::PlanBuilder planner_;
  std::map<int, Job> jobs_;
  std::map<pid_t, int> pid_to_job_;
  int next_job_id_{1};
  bool interactive_{false};
  bool exit_requested_{false};
  int shell_terminal_{0};
  int signal_read_{-1};
  int signal_write_{-1};
  bool signal_bridge_installed_{false};
  struct sigaction old_sigchld_{};
  struct sigaction old_sigwinch_{};
  inspect::Trace* trace_{nullptr};
  static int signal_write_fd_;
};

}  // namespace splice::runtime
