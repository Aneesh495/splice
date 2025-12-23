#include "runtime/runtime.hpp"

#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"
#include "expand/pattern.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fnmatch.h>
#include <regex.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

extern "C" char** environ;

namespace splice::runtime {

int Runtime::signal_write_fd_ = -1;

namespace {

struct ChildError {
  int operation{0};
  int error_number{0};
};

constexpr int kErrorOpen = 1;
constexpr int kErrorDup = 2;
constexpr int kErrorExec = 3;

void write_child_error(int fd, int operation, int error_number) {
  const ChildError value{operation, error_number};
  const char* bytes = reinterpret_cast<const char*>(&value);
  std::size_t offset = 0;
  while (offset < sizeof(value)) {
    const ssize_t written = ::write(fd, bytes + offset, sizeof(value) - offset);
    if (written <= 0) break;
    offset += static_cast<std::size_t>(written);
  }
}

void close_if_open(int fd) {
  if (fd >= 0) ::close(fd);
}

bool set_cloexec(int fd) {
  const int flags = fcntl(fd, F_GETFD);
  return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

}  // namespace

Runtime::Runtime(expand::ShellState& state, inspect::Trace* trace)
    : state_(state), expander_(state, [this](const std::string& source, int& status) {
        return substitute(source, status);
      }), planner_(state, [this](const std::string& source, int& status) {
        return substitute(source, status);
      }), trace_(trace) {
  shell_terminal_ = STDIN_FILENO;
  interactive_ = isatty(shell_terminal_) != 0 && tcgetpgrp(shell_terminal_) == getpgrp();
  if (interactive_) {
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
  }
  install_signal_bridge();
  if (trace_ != nullptr) (void)trace_->record("runtime-created", {{"interactive", interactive_ ? "true" : "false"}});
}

void Runtime::signal_handler(int signal_number) noexcept {
  const int fd = signal_write_fd_;
  if (fd < 0) return;
  const unsigned char value = static_cast<unsigned char>(signal_number);
  const ssize_t ignored = ::write(fd, &value, sizeof(value));
  (void)ignored;
}

void Runtime::install_signal_bridge() {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) return;
  signal_read_ = pipe_fds[0];
  signal_write_ = pipe_fds[1];
  const int read_flags = fcntl(signal_read_, F_GETFL);
  const int write_flags = fcntl(signal_write_, F_GETFL);
  if (read_flags >= 0) fcntl(signal_read_, F_SETFL, read_flags | O_NONBLOCK);
  if (write_flags >= 0) fcntl(signal_write_, F_SETFL, write_flags | O_NONBLOCK);
  set_cloexec(signal_read_);
  set_cloexec(signal_write_);
  struct sigaction action{};
  action.sa_handler = &Runtime::signal_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &action, &old_sigchld_) != 0) return;
  if (sigaction(SIGWINCH, &action, &old_sigwinch_) != 0) {
    sigaction(SIGCHLD, &old_sigchld_, nullptr);
    return;
  }
  signal_write_fd_ = signal_write_;
  signal_bridge_installed_ = true;
}

void Runtime::restore_signal_bridge() {
  if (signal_bridge_installed_) {
    sigaction(SIGCHLD, &old_sigchld_, nullptr);
    sigaction(SIGWINCH, &old_sigwinch_, nullptr);
    if (signal_write_fd_ == signal_write_) signal_write_fd_ = -1;
  }
  close_if_open(signal_read_);
  close_if_open(signal_write_);
  signal_read_ = -1;
  signal_write_ = -1;
  signal_bridge_installed_ = false;
}

void Runtime::drain_signal_bridge() {
  if (signal_read_ < 0) return;
  std::array<unsigned char, 64> buffer{};
  while (read(signal_read_, buffer.data(), buffer.size()) > 0) {}
}

Runtime::~Runtime() {
  restore_signal_bridge();
  for (auto& [id, job] : jobs_) {
    if (job.state != JobState::Completed) {
      if (job.process_group > 0) ::kill(-job.process_group, SIGHUP);
      for (const auto process : job.processes) {
        int status = 0;
        while (waitpid(process.value, &status, 0) < 0 && errno == EINTR) {}
      }
    }
  }
}

void Runtime::report_error(std::string_view message) const {
  const std::string text = "splice: " + std::string(message) + "\n";
  ::write(STDERR_FILENO, text.data(), text.size());
}

int Runtime::decode_status(int status) const noexcept {
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  if (WIFSTOPPED(status)) return 128 + WSTOPSIG(status);
  return 1;
}

std::string Runtime::resolve_executable(const std::string& name) const {
  if (name.find('/') != std::string::npos) return name;
  const std::string path = state_.value("PATH");
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t separator = path.find(':', start);
    const std::string entry = path.substr(start, separator == std::string::npos ? std::string::npos : separator - start);
    const std::string candidate = (entry.empty() ? "." : entry) + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
    if (separator == std::string::npos) break;
    start = separator + 1;
  }
  return {};
}

std::vector<std::string> Runtime::environment_for(const plan::PlannedCommand& command) const {
  std::vector<std::string> result = command.environment;
  for (const auto& [name, value] : command.assignments) {
    const std::string prefix = name + "=";
    auto found = std::find_if(result.begin(), result.end(), [&](const std::string& entry) {
      return entry.starts_with(prefix);
    });
    if (found == result.end()) result.push_back(prefix + value);
    else *found = prefix + value;
  }
  return result;
}

int Runtime::apply_descriptor_actions(const plan::PlannedCommand& command) {
  for (const auto& action : command.descriptors) {
    if (action.kind == plan::DescriptorActionKind::Open || action.kind == plan::DescriptorActionKind::Append) {
      int flags = action.flags;
      if (state_.option(expand::ShellOption::Noclobber) && action.kind == plan::DescriptorActionKind::Open) flags |= O_EXCL;
      const int fd = ::open(action.path.c_str(), flags, 0666);
      if (fd < 0) return -kErrorOpen;
      if (fd != action.fd && dup2(fd, action.fd) < 0) {
        const int saved = errno;
        close_if_open(fd);
        errno = saved;
        return -kErrorDup;
      }
      if (fd != action.fd) close_if_open(fd);
    } else if (action.kind == plan::DescriptorActionKind::Duplicate) {
      if (dup2(action.target_fd, action.fd) < 0) return -kErrorDup;
    } else if (action.kind == plan::DescriptorActionKind::Close) {
      if (close(action.fd) < 0 && errno != EBADF) return -kErrorDup;
    } else if (action.kind == plan::DescriptorActionKind::HereDocument) {
      char temporary[] = "/tmp/splice-heredoc-XXXXXX";
      const int body_fd = mkstemp(temporary);
      if (body_fd < 0) return -kErrorOpen;
      unlink(temporary);
      std::size_t offset = 0;
      while (offset < action.body.size()) {
        const ssize_t written = write(body_fd, action.body.data() + offset, action.body.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
          close(body_fd);
          return -kErrorOpen;
        }
        offset += static_cast<std::size_t>(written);
      }
      if (lseek(body_fd, 0, SEEK_SET) < 0 || dup2(body_fd, action.fd) < 0) {
        close(body_fd);
        return -kErrorDup;
      }
      close(body_fd);
    }
  }
  return 0;
}

int Runtime::apply_parent_descriptors(const plan::PlannedCommand& command, std::vector<int>& saved) {
  saved.assign(3, -3);
  for (int fd = 0; fd < 3; ++fd) {
    saved[static_cast<std::size_t>(fd)] = dup(fd);
    if (saved[static_cast<std::size_t>(fd)] < 0 && errno != EBADF) {
      saved[static_cast<std::size_t>(fd)] = -2;
      return -1;
    }
  }
  return apply_descriptor_actions(command);
}

void Runtime::restore_parent_descriptors(const std::vector<int>& saved) {
  for (int fd = 0; fd < 3 && static_cast<std::size_t>(fd) < saved.size(); ++fd) {
    if (saved[static_cast<std::size_t>(fd)] == -2 || saved[static_cast<std::size_t>(fd)] == -3) continue;
    if (saved[static_cast<std::size_t>(fd)] >= 0) {
      dup2(saved[static_cast<std::size_t>(fd)], fd);
      close(saved[static_cast<std::size_t>(fd)]);
    } else {
      close(fd);
    }
  }
}

int Runtime::apply_parent_redirections(const std::vector<syntax::Redirection>& redirections,
                                       std::vector<int>& saved) {
  syntax::SimpleCommand simple;
  simple.redirections = redirections;
  syntax::Command cmd;
  cmd.kind = syntax::CommandKind::Simple;
  cmd.simple = simple;
  const auto plan = planner_.build(std::make_shared<syntax::Command>(cmd));
  if (!plan.valid() || plan.stages.empty()) return 0;
  return apply_parent_descriptors(plan.stages.front(), saved);
}

int Runtime::child_setup_and_exec(const plan::PlannedCommand& command,
                                  int input_fd, int output_fd, int error_fd,
                                  int error_pipe) {
  if (input_fd != STDIN_FILENO && dup2(input_fd, STDIN_FILENO) < 0) {
    write_child_error(error_pipe, kErrorDup, errno);
    _exit(125);
  }
  if (output_fd != STDOUT_FILENO && dup2(output_fd, STDOUT_FILENO) < 0) {
    write_child_error(error_pipe, kErrorDup, errno);
    _exit(125);
  }
  if (error_fd != STDERR_FILENO && dup2(error_fd, STDERR_FILENO) < 0) {
    write_child_error(error_pipe, kErrorDup, errno);
    _exit(125);
  }
  close_if_open(input_fd == STDIN_FILENO ? -1 : input_fd);
  if (output_fd != input_fd) close_if_open(output_fd == STDOUT_FILENO ? -1 : output_fd);
  if (error_fd != input_fd && error_fd != output_fd) close_if_open(error_fd == STDERR_FILENO ? -1 : error_fd);
  if (apply_descriptor_actions(command) != 0) {
    write_child_error(error_pipe, kErrorOpen, errno);
    _exit(125);
  }
  for (const auto& [name, value] : command.assignments) setenv(name.c_str(), value.c_str(), 1);
  if (command.argv.empty()) _exit(0);
  if (builtins::is_builtin(command.argv.front())) {
    builtins::Context context{state_, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, false, {}, {}, {}};
    const auto result = builtins::run(command.argv, context);
    _exit(result.status);
  }
  const std::string executable = resolve_executable(command.argv.front());
  if (executable.empty()) {
    write_child_error(error_pipe, kErrorExec, ENOENT);
    _exit(127);
  }
  std::vector<char*> argv;
  argv.reserve(command.argv.size() + 1);
  for (const auto& value : command.argv) argv.push_back(const_cast<char*>(value.c_str()));
  argv.push_back(nullptr);
  const auto environment = environment_for(command);
  std::vector<char*> envp;
  envp.reserve(environment.size() + 1);
  for (const auto& value : environment) envp.push_back(const_cast<char*>(value.c_str()));
  envp.push_back(nullptr);
  execve(executable.c_str(), argv.data(), envp.data());
  write_child_error(error_pipe, kErrorExec, errno);
  _exit(errno == EACCES ? 126 : 127);
}

int Runtime::execute_parent_builtin(const plan::PlannedCommand& command) {
  std::vector<int> saved;
  if (apply_parent_descriptors(command, saved) != 0) {
    restore_parent_descriptors(saved);
    report_error("redirection failed for builtin");
    return 1;
  }
  for (const auto& [name, value] : command.assignments) {
    (void)value;
    if (state_.is_readonly(name)) {
      restore_parent_descriptors(saved);
      report_error("assignment failed for readonly variable");
      return 1;
    }
  }
  for (const auto& [name, value] : command.assignments) {
    if (!state_.set(name, value)) {
      restore_parent_descriptors(saved);
      report_error("assignment failed for readonly variable");
      return 1;
    }
  }
  builtins::Context context{state_, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, true,
                            [this](const std::vector<std::string>& argv) {
                              plan::ExecutionPlan nested_plan;
                              plan::PlannedCommand nested;
                              nested.argv = argv;
                              nested.environment = state_.environment();
                              nested_plan.stages.push_back(std::move(nested));
                              return execute_plan(nested_plan);
                            },
                            [this](const std::vector<std::string>& argv) {
                              return job_control(argv);
                            },
                            [this](const std::string& script) {
                              return execute_string(script);
                            }};
  const auto result = builtins::run(command.argv, context);
  restore_parent_descriptors(saved);
  if (result.request_exit) exit_requested_ = true;
  return result.status;
}

int Runtime::execute_function(const plan::PlannedCommand& command, const syntax::CommandPtr& func_body) {
  std::vector<int> saved;
  if (apply_parent_descriptors(command, saved) != 0) {
    restore_parent_descriptors(saved);
    report_error("redirection failed for function call");
    return 1;
  }
  std::vector<std::string> new_positional;
  for (std::size_t i = 1; i < command.argv.size(); ++i) {
    new_positional.push_back(command.argv[i]);
  }
  state_.push_function_frame(std::move(new_positional));
  for (const auto& [vname, vval] : command.assignments) {
    state_.set_local(vname, vval);
  }
  int status = execute_command(func_body);
  if (state_.return_requested) {
    status = state_.return_status;
    state_.return_requested = false;
  }
  state_.pop_function_frame();
  restore_parent_descriptors(saved);
  return status;
}

int Runtime::job_control(const std::vector<std::string>& argv) {
  auto find_requested = [&](std::string_view request) -> Job* {
    if (request.starts_with("%")) {
      try {
        const int id = std::stoi(std::string(request.substr(1)));
        const auto iterator = jobs_.find(id);
        return iterator == jobs_.end() ? nullptr : &iterator->second;
      } catch (...) {
        return nullptr;
      }
    }
    try {
      const pid_t pid = static_cast<pid_t>(std::stol(std::string(request)));
      return find_job_for_pid(pid);
    } catch (...) {
      return nullptr;
    }
  };
  const std::string_view operation = argv.empty() ? std::string_view{} : argv.front();
  if (operation == "jobs") {
    for (const auto& [id, job] : jobs_) {
      const char* state = job.state == JobState::Running ? "Running" :
                          job.state == JobState::Stopped ? "Stopped" : "Done";
      const std::string line = "[" + std::to_string(id) + "] " + state + " pgid=" +
                               std::to_string(job.process_group) + "\n";
      ::write(STDOUT_FILENO, line.data(), line.size());
    }
    return 0;
  }
  if (operation == "disown") {
    if (argv.size() == 1) {
      jobs_.clear();
      pid_to_job_.clear();
      return 0;
    }
    Job* job = find_requested(argv[1]);
    if (job == nullptr) return 1;
    const int id = job->id;
    for (const auto process : job->processes) pid_to_job_.erase(process.value);
    jobs_.erase(id);
    return 0;
  }
  auto wait_job = [&](Job& job) {
    if (job.state == JobState::Stopped) return job.status;
    while (job.results.size() < job.processes.size()) {
      int status = 0;
      const pid_t pid = waitpid(-1, &status, 0);
      if (pid < 0) {
        if (errno == EINTR) continue;
        break;
      }
      reap_one(pid, status);
      if (job.state == JobState::Stopped) return job.status;
    }
    return job.status;
  };
  if (operation == "wait") {
    if (argv.size() == 1) {
      int status = 0;
      for (auto& [id, job] : jobs_) {
        (void)id;
        if (job.state != JobState::Completed) status = wait_job(job);
        else status = job.status;
      }
      return status;
    }
    Job* job = find_requested(argv[1]);
    return job == nullptr ? 127 : wait_job(*job);
  }
  Job* job = argv.size() > 1 ? find_requested(argv[1]) : (jobs_.empty() ? nullptr : &jobs_.rbegin()->second);
  if (job == nullptr) return 1;
  if (operation == "bg") {
    if (job->process_group > 0) ::kill(-job->process_group, SIGCONT);
    job->state = JobState::Running;
    return 0;
  }
  if (operation == "fg") {
    if (interactive_ && job->process_group > 0) tcsetpgrp(shell_terminal_, job->process_group);
    if (job->process_group > 0) ::kill(-job->process_group, SIGCONT);
    job->state = JobState::Running;
    return wait_foreground(*job);
  }
  return 2;
}

void Runtime::register_job(Job job) {
  const int id = job.id;
  for (const auto process : job.processes) pid_to_job_[process.value] = id;
  jobs_.emplace(id, std::move(job));
}

Job* Runtime::find_job_for_pid(pid_t pid) {
  const auto iterator = pid_to_job_.find(pid);
  if (iterator == pid_to_job_.end()) return nullptr;
  const auto job = jobs_.find(iterator->second);
  return job == jobs_.end() ? nullptr : &job->second;
}

void Runtime::reap_one(pid_t pid, int status) {
  Job* job = find_job_for_pid(pid);
  if (job == nullptr) return;
  const auto process = std::find_if(job->processes.begin(), job->processes.end(),
                                    [pid](ProcessId value) { return value.value == pid; });
  const std::size_t stage = process == job->processes.end()
      ? 0 : static_cast<std::size_t>(std::distance(job->processes.begin(), process));
  if (WIFSTOPPED(status)) {
    job->state = JobState::Stopped;
    job->status = 128 + WSTOPSIG(status);
    if (trace_ != nullptr) (void)trace_->record("child-stopped", {{"pid", std::to_string(pid)}, {"status", std::to_string(job->status)}});
    return;
  }
  if (WIFCONTINUED(status)) {
    job->state = JobState::Running;
    if (trace_ != nullptr) (void)trace_->record("child-continued", {{"pid", std::to_string(pid)}});
    return;
  }
  if (!WIFEXITED(status) && !WIFSIGNALED(status)) return;
  if (job->stage_statuses.size() < job->processes.size()) job->stage_statuses.assign(job->processes.size(), -1);
  if (job->stage_statuses[stage] != -1) return;
  const int decoded = decode_status(status);
  job->stage_statuses[stage] = decoded;
  job->results.push_back(ProcessResult{ProcessId{pid}, decoded, WIFEXITED(status), WIFSIGNALED(status)});
  if (trace_ != nullptr) (void)trace_->record("child-status", {{"pid", std::to_string(pid)}, {"stage", std::to_string(stage)}, {"status", std::to_string(decoded)}});
  const bool all_done = std::all_of(job->stage_statuses.begin(), job->stage_statuses.end(), [](int value) { return value >= 0; });
  if (all_done) {
    job->state = JobState::Completed;
    job->status = job->stage_statuses.back();
    if (state_.option(expand::ShellOption::Pipefail)) {
      for (auto iterator = job->stage_statuses.rbegin(); iterator != job->stage_statuses.rend(); ++iterator) {
        if (*iterator != 0) { job->status = *iterator; break; }
      }
    }
    state_.pipe_status = job->stage_statuses;
    std::vector<std::string> pipe_strs;
    for (int s : job->stage_statuses) pipe_strs.push_back(std::to_string(s));
    state_.set_array("PIPESTATUS", std::move(pipe_strs));
  }
}

void Runtime::reap_nonblocking() {
  drain_signal_bridge();
  while (true) {
    int status = 0;
    const pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
    if (pid <= 0) break;
    reap_one(pid, status);
  }
}

int Runtime::wait_foreground(Job& job) {
  while (job.results.size() < job.processes.size()) {
    int status = 0;
    const pid_t pid = waitpid(-1, &status, WUNTRACED);
    if (pid < 0) {
      if (errno == EINTR) continue;
      break;
    }
    reap_one(pid, status);
    if (job.state == JobState::Stopped) break;
  }
  if (interactive_ && job.process_group > 0) tcsetpgrp(shell_terminal_, getpgrp());
  return job.status;
}

bool Runtime::try_fast_spawn(const plan::PlannedCommand& command, int input_fd,
                             int output_fd, int error_fd, pid_t group, pid_t& pid) {
  if (command.argv.empty()) return false;
  if (builtins::is_builtin(command.argv.front())) return false;
  if (state_.functions.find(command.argv.front()) != state_.functions.end()) return false;

  const std::string executable = resolve_executable(command.argv.front());
  if (executable.empty()) return false;

  posix_spawn_file_actions_t file_actions;
  if (posix_spawn_file_actions_init(&file_actions) != 0) return false;

  posix_spawnattr_t attr;
  if (posix_spawnattr_init(&attr) != 0) {
    posix_spawn_file_actions_destroy(&file_actions);
    return false;
  }

  short flags = POSIX_SPAWN_SETPGROUP;
  posix_spawnattr_setflags(&attr, flags);
  posix_spawnattr_setpgroup(&attr, group > 0 ? group : 0);

  if (input_fd != STDIN_FILENO && input_fd >= 0) {
    posix_spawn_file_actions_adddup2(&file_actions, input_fd, STDIN_FILENO);
  }
  if (output_fd != STDOUT_FILENO && output_fd >= 0) {
    posix_spawn_file_actions_adddup2(&file_actions, output_fd, STDOUT_FILENO);
  }
  if (error_fd != STDERR_FILENO && error_fd >= 0) {
    posix_spawn_file_actions_adddup2(&file_actions, error_fd, STDERR_FILENO);
  }

  std::vector<int> pipe_heredocs_to_close;
  bool ok = true;
  for (const auto& action : command.descriptors) {
    if (action.kind == plan::DescriptorActionKind::Open || action.kind == plan::DescriptorActionKind::Append) {
      int open_flags = action.flags;
      if (state_.option(expand::ShellOption::Noclobber) && action.kind == plan::DescriptorActionKind::Open) {
        open_flags |= O_EXCL;
      }
      if (posix_spawn_file_actions_addopen(&file_actions, action.fd, action.path.c_str(), open_flags, 0666) != 0) {
        ok = false;
        break;
      }
    } else if (action.kind == plan::DescriptorActionKind::Duplicate) {
      if (posix_spawn_file_actions_adddup2(&file_actions, action.target_fd, action.fd) != 0) {
        ok = false;
        break;
      }
    } else if (action.kind == plan::DescriptorActionKind::Close) {
      if (posix_spawn_file_actions_addclose(&file_actions, action.fd) != 0) {
        ok = false;
        break;
      }
    } else if (action.kind == plan::DescriptorActionKind::HereDocument) {
      int hpipe[2];
      if (pipe(hpipe) != 0) { ok = false; break; }
      std::size_t offset = 0;
      while (offset < action.body.size()) {
        const ssize_t w = write(hpipe[1], action.body.data() + offset, action.body.size() - offset);
        if (w <= 0) break;
        offset += static_cast<std::size_t>(w);
      }
      close(hpipe[1]);
      posix_spawn_file_actions_adddup2(&file_actions, hpipe[0], action.fd);
      posix_spawn_file_actions_addclose(&file_actions, hpipe[0]);
      pipe_heredocs_to_close.push_back(hpipe[0]);
    }
  }

  if (!ok) {
    for (int pfd : pipe_heredocs_to_close) close(pfd);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&file_actions);
    return false;
  }

  std::vector<char*> argv;
  argv.reserve(command.argv.size() + 1);
  for (const auto& a : command.argv) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  char** envp_ptr = ::environ;
  std::vector<std::string> environment;
  std::vector<char*> envp;
  if (!command.assignments.empty()) {
    environment = environment_for(command);
    envp.reserve(environment.size() + 1);
    for (const auto& e : environment) envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);
    envp_ptr = envp.data();
  }

  pid_t spawned_pid = -1;
  int err = posix_spawn(&spawned_pid, executable.c_str(), &file_actions, &attr, argv.data(), envp_ptr);
  for (int pfd : pipe_heredocs_to_close) close(pfd);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&file_actions);

  if (err != 0) return false;
  pid = spawned_pid;
  return true;
}

int Runtime::launch_pipeline(const plan::ExecutionPlan& plan) {
  if (plan.stages.size() == 1) {
    pid_t fast_pid = -1;
    const int child_error = plan.stages.front().merge_stderr ? STDOUT_FILENO : STDERR_FILENO;
    if (try_fast_spawn(plan.stages.front(), STDIN_FILENO, STDOUT_FILENO, child_error, -1, fast_pid)) {
      Job job;
      job.id = next_job_id_++;
      job.background = plan.background;
      job.process_group = fast_pid;
      job.processes.push_back(ProcessId{fast_pid});
      job.stage_statuses.assign(1, -1);
      register_job(job);
      if (trace_ != nullptr) (void)trace_->record("pipeline-registered", {{"job", std::to_string(job.id)}, {"pgid", std::to_string(fast_pid)}, {"stages", "1"}});
      if (!plan.background && interactive_ && fast_pid > 0) tcsetpgrp(shell_terminal_, fast_pid);
      if (plan.background) {
        state_.last_background_pid = fast_pid;
        return 0;
      }
      Job& stored = jobs_.at(job.id);
      return wait_foreground(stored);
    }
  }
  sigset_t blocked{};
  sigset_t previous{};
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGCHLD);
  const bool signals_blocked = sigprocmask(SIG_BLOCK, &blocked, &previous) == 0;
  const auto unblock_signals = [&]() {
    if (signals_blocked) sigprocmask(SIG_SETMASK, &previous, nullptr);
  };
  std::vector<std::array<int, 2>> pipes;
  if (plan.stages.size() > 1) {
    pipes.resize(plan.stages.size() - 1);
    for (auto& pair : pipes) pair = {-1, -1};
    for (auto& pair : pipes) {
      if (pipe(pair.data()) != 0) {
        report_error(std::string("pipe: ") + std::strerror(errno));
        for (auto& created : pipes) {
          close_if_open(created[0]);
          close_if_open(created[1]);
        }
        unblock_signals();
        return 125;
      }
      set_cloexec(pair[0]);
      set_cloexec(pair[1]);
    }
  }
  int error_read = -1;
  int error_write = -1;
  int error_pipe[2];
  if (pipe(error_pipe) != 0) {
    for (auto& pair : pipes) {
      close_if_open(pair[0]);
      close_if_open(pair[1]);
    }
    unblock_signals();
    return 125;
  }
  error_read = error_pipe[0];
  error_write = error_pipe[1];
  set_cloexec(error_write);
  Job job;
  job.id = next_job_id_++;
  job.background = plan.background;
  job.stage_statuses.assign(plan.stages.size(), -1);
  pid_t group = -1;
  for (std::size_t index = 0; index < plan.stages.size(); ++index) {
    const int input = index == 0 ? STDIN_FILENO : pipes[index - 1][0];
    const int output = index + 1 == plan.stages.size() ? STDOUT_FILENO : pipes[index][1];
    const int child_error = plan.stages[index].merge_stderr ? output : STDERR_FILENO;

    pid_t fast_pid = -1;
    if (try_fast_spawn(plan.stages[index], input, output, child_error, group, fast_pid)) {
      if (group < 0) group = fast_pid;
      job.processes.push_back(ProcessId{fast_pid});
      if (index == 0) job.process_group = group;
      if (index > 0) close(pipes[index - 1][0]);
      if (index + 1 < plan.stages.size()) close(pipes[index][1]);
      continue;
    }

    pid_t pid = fork();
    if (pid == 0) {
      signal(SIGTTOU, SIG_DFL);
      signal(SIGTTIN, SIG_DFL);
      signal(SIGTSTP, SIG_DFL);
      sigprocmask(SIG_SETMASK, &previous, nullptr);
      if (group < 0) group = getpid();
      setpgid(0, group);
      close(error_read);
      for (std::size_t close_index = 0; close_index < pipes.size(); ++close_index) {
        if (static_cast<int>(close_index) != static_cast<int>(index) - 1) close_if_open(pipes[close_index][0]);
        if (static_cast<int>(close_index) != static_cast<int>(index)) close_if_open(pipes[close_index][1]);
      }
      (void)child_setup_and_exec(plan.stages[index], input, output, child_error, error_write);
      _exit(125);
    }
    if (pid < 0) {
      report_error(std::string("fork: ") + std::strerror(errno));
      if (group > 0) kill(-group, SIGTERM);
      usleep(100000);
      if (group > 0) kill(-group, SIGKILL);
      for (const auto process : job.processes) {
        int child_status = 0;
        while (waitpid(process.value, &child_status, 0) < 0 && errno == EINTR) {}
      }
      for (auto& pair : pipes) {
        close_if_open(pair[0]);
        close_if_open(pair[1]);
      }
      close(error_read);
      close(error_write);
      unblock_signals();
      return 125;
    }
    if (group < 0) group = pid;
    setpgid(pid, group);
    job.processes.push_back(ProcessId{pid});
    if (index == 0) job.process_group = group;
    if (index > 0) close(pipes[index - 1][0]);
    if (index + 1 < plan.stages.size()) close(pipes[index][1]);
  }
  for (auto& pair : pipes) {
    close_if_open(pair[0]);
    close_if_open(pair[1]);
  }
  close(error_write);
  job.process_group = group;
  register_job(job);
  if (trace_ != nullptr) (void)trace_->record("pipeline-registered", {{"job", std::to_string(job.id)}, {"pgid", std::to_string(group)}, {"stages", std::to_string(job.processes.size())}});
  unblock_signals();
  Job& stored = jobs_.at(job.id);
  if (!plan.background && interactive_ && group > 0) tcsetpgrp(shell_terminal_, group);
  if (plan.background) {
    close(error_read);
    state_.last_background_pid = group;
    return 0;
  }
  const int status = wait_foreground(stored);
  char error_bytes[sizeof(ChildError)];
  const ssize_t error_size = read(error_read, error_bytes, sizeof(error_bytes));
  close(error_read);
  if (error_size == static_cast<ssize_t>(sizeof(ChildError))) {
    const auto* error_value = reinterpret_cast<const ChildError*>(error_bytes);
    report_error(std::string("child launch failed: ") + std::strerror(error_value->error_number));
    return error_value->operation == kErrorExec ? (error_value->error_number == EACCES ? 126 : 127) : 125;
  }
  return status;
}

int Runtime::execute_plan(const plan::ExecutionPlan& plan) {
  if (!plan.valid()) {
    report_error(plan.error.empty() ? "invalid execution plan" : plan.error);
    return 2;
  }
  if (plan.stages.size() == 1 && !plan.background) {
    if (plan.stages.front().argv.empty()) {
      return execute_parent_builtin(plan.stages.front());
    }
    const std::string& name = plan.stages.front().argv.front();
    const auto func_it = state_.functions.find(name);
    if (func_it != state_.functions.end()) {
      return execute_function(plan.stages.front(), func_it->second);
    }
    if (builtins::is_builtin(name)) {
      return execute_parent_builtin(plan.stages.front());
    }
  }
  return launch_pipeline(plan);
}

int Runtime::execute_subshell(const syntax::CommandPtr& command) {
  if (command->children.empty()) return 0;
  const pid_t pid = fork();
  if (pid < 0) {
    report_error(std::string("fork subshell: ") + std::strerror(errno));
    return 125;
  }
  if (pid == 0) {
    expand::ShellState child_state = state_;
    Runtime child_runtime(child_state);
    const int status = child_runtime.execute_command(command->children.front());
    _exit(status);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return 125;
  }
  return decode_status(status);
}

int Runtime::execute_and_or(const syntax::CommandPtr& command) {
  int status = 0;
  for (std::size_t index = 0; index < command->children.size(); ++index) {
    if (index > 0) {
      const std::string& operation = command->operators[index - 1];
      if (operation == "&&" && status != 0) continue;
      if (operation == "||" && status == 0) continue;
    }
    status = execute_command(command->children[index]);
  }
  return command->negated ? (status == 0 ? 1 : 0) : status;
}

int Runtime::execute_sequence(const syntax::CommandPtr& command) {
  int status = 0;
  for (const auto& child : command->children) {
    status = execute_command(child);
    if (exit_requested_ || state_.return_requested) break;
    if (state_.break_levels > 0 || state_.continue_levels > 0) break;
    if (state_.option(expand::ShellOption::Errexit) && status != 0) break;
  }
  return status;
}

int Runtime::execute_if(const syntax::CommandPtr& command) {
  if (!command->if_cmd) return 0;
  bool matched = false;
  int status = 0;
  for (const auto& clause : command->if_cmd->clauses) {
    const int cond_status = execute_command(clause.condition);
    if (cond_status == 0) {
      status = execute_command(clause.body);
      matched = true;
      break;
    }
  }
  if (!matched && command->if_cmd->else_body) {
    status = execute_command(command->if_cmd->else_body);
  }
  return status;
}

int Runtime::execute_for(const syntax::CommandPtr& command) {
  if (!command->for_cmd) return 0;
  int status = 0;
  if (command->for_cmd->is_arithmetic) {
    if (!command->for_cmd->arith_init.empty()) {
      long long val = 0;
      expand::evaluate_arithmetic(command->for_cmd->arith_init, val, &state_);
    }
    while (true) {
      if (!command->for_cmd->arith_cond.empty()) {
        long long cond_val = 0;
        expand::evaluate_arithmetic(command->for_cmd->arith_cond, cond_val, &state_);
        if (cond_val == 0) break;
      }
      if (command->for_cmd->body) {
        status = execute_command(command->for_cmd->body);
      }
      if (exit_requested_ || state_.return_requested) break;
      if (state_.break_levels > 0) {
        --state_.break_levels;
        break;
      }
      if (state_.continue_levels > 0) {
        --state_.continue_levels;
        if (state_.continue_levels > 0) break;
      }
      if (!command->for_cmd->arith_step.empty()) {
        long long step_val = 0;
        expand::evaluate_arithmetic(command->for_cmd->arith_step, step_val, &state_);
      }
    }
    return status;
  }

  std::vector<std::string> words;
  for (const auto& w : command->for_cmd->words) {
    auto res = expander_.word(w);
    for (const auto& f : res.fields) words.push_back(f.value);
  }

  for (const auto& item : words) {
    state_.set(command->for_cmd->name, item);
    if (command->for_cmd->body) {
      status = execute_command(command->for_cmd->body);
    }
    if (exit_requested_ || state_.return_requested) break;
    if (state_.break_levels > 0) {
      --state_.break_levels;
      break;
    }
    if (state_.continue_levels > 0) {
      --state_.continue_levels;
      if (state_.continue_levels > 0) break;
    }
  }
  return status;
}

int Runtime::execute_while(const syntax::CommandPtr& command) {
  if (!command->while_cmd) return 0;
  const bool until = command->while_cmd->until;
  int status = 0;
  while (true) {
    const int cond_status = execute_command(command->while_cmd->condition);
    if (exit_requested_ || state_.return_requested) break;
    if ((cond_status == 0) == until) break;
    if (command->while_cmd->body) {
      status = execute_command(command->while_cmd->body);
    }
    if (exit_requested_ || state_.return_requested) break;
    if (state_.break_levels > 0) {
      --state_.break_levels;
      break;
    }
    if (state_.continue_levels > 0) {
      --state_.continue_levels;
      if (state_.continue_levels > 0) break;
    }
  }
  return status;
}

int Runtime::execute_case(const syntax::CommandPtr& command) {
  if (!command->case_cmd) return 0;
  auto word_res = expander_.word(command->case_cmd->word);
  const std::string target = word_res.fields.empty() ? "" : word_res.fields.front().value;
  int status = 0;
  bool fallthrough = false;
  for (const auto& item : command->case_cmd->items) {
    bool matched = fallthrough;
    if (!matched) {
      for (const auto& pat_word : item.patterns) {
        auto pat_res = expander_.word(pat_word);
        const std::string pat = pat_res.fields.empty() ? "" : pat_res.fields.front().value;
        if (expand::pattern_match(pat, target, true)) {
          matched = true;
          break;
        }
      }
    }
    if (matched) {
      if (item.body) status = execute_command(item.body);
      if (item.terminator == ";&") {
        fallthrough = true;
        continue;
      }
      if (item.terminator == ";;&") {
        fallthrough = false;
        continue;
      }
      break;
    }
    fallthrough = false;
  }
  return status;
}

int Runtime::execute_cond(const syntax::CondNodePtr& cond) {
  if (cond == nullptr) return 1;
  if (cond->op == syntax::CondOp::Not) {
    return execute_cond(cond->left_child) == 0 ? 1 : 0;
  }
  if (cond->op == syntax::CondOp::And) {
    return (execute_cond(cond->left_child) == 0 && execute_cond(cond->right_child) == 0) ? 0 : 1;
  }
  if (cond->op == syntax::CondOp::Or) {
    return (execute_cond(cond->left_child) == 0 || execute_cond(cond->right_child) == 0) ? 0 : 1;
  }
  if (cond->op == syntax::CondOp::Unary) {
    auto res = expander_.word(cond->left);
    const std::string arg = res.fields.empty() ? "" : res.fields.front().value;
    builtins::Context ctx{state_, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, false, {}, {}, {}};
    return builtins::run({"test", cond->op_text, arg}, ctx).status;
  }
  if (cond->op == syntax::CondOp::Binary) {
    auto left_res = expander_.word(cond->left);
    auto right_res = expander_.word(cond->right);
    const std::string left = left_res.fields.empty() ? "" : left_res.fields.front().value;
    const std::string right = right_res.fields.empty() ? "" : right_res.fields.front().value;

    if (cond->op_text == "=~") {
      regex_t reg{};
      if (regcomp(&reg, right.c_str(), REG_EXTENDED) != 0) return 2;
      const int match = regexec(&reg, left.c_str(), 0, nullptr, 0);
      regfree(&reg);
      return match == 0 ? 0 : 1;
    }
    if (cond->op_text == "==" || cond->op_text == "=") {
      return expand::pattern_match(right, left, true) ? 0 : 1;
    }
    if (cond->op_text == "!=") {
      return !expand::pattern_match(right, left, true) ? 0 : 1;
    }
    builtins::Context ctx{state_, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, false, {}, {}, {}};
    return builtins::run({"test", left, cond->op_text, right}, ctx).status;
  }
  return 1;
}

int Runtime::execute_arith_command(const syntax::CommandPtr& command) {
  long long result = 0;
  expand::evaluate_arithmetic(command->arith_expr, result, &state_);
  return result != 0 ? 0 : 1;
}

int Runtime::execute_time(const syntax::CommandPtr& command) {
  if (!command->time_cmd || !command->time_cmd->command) return 0;
  const auto start = std::chrono::steady_clock::now();
  struct rusage r_start{};
  getrusage(RUSAGE_CHILDREN, &r_start);

  const int status = execute_command(command->time_cmd->command);

  const auto end = std::chrono::steady_clock::now();
  struct rusage r_end{};
  getrusage(RUSAGE_CHILDREN, &r_end);

  const auto real_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  const long long user_ms = (r_end.ru_utime.tv_sec - r_start.ru_utime.tv_sec) * 1000 +
                            (r_end.ru_utime.tv_usec - r_start.ru_utime.tv_usec) / 1000;
  const long long sys_ms = (r_end.ru_stime.tv_sec - r_start.ru_stime.tv_sec) * 1000 +
                           (r_end.ru_stime.tv_usec - r_start.ru_stime.tv_usec) / 1000;

  auto fmt = [](long long total_ms) {
    long long m = total_ms / 60000;
    long long s = (total_ms % 60000) / 1000;
    long long ms = total_ms % 1000;
    std::ostringstream ss;
    ss << m << "m" << s << "." << std::setfill('0') << std::setw(3) << ms << "s";
    return ss.str();
  };

  std::string output = "real\t" + fmt(real_ms) + "\nuser\t" + fmt(user_ms) + "\nsys\t" + fmt(sys_ms) + "\n";
  ::write(STDERR_FILENO, output.data(), output.size());
  return status;
}

int Runtime::execute_command(const syntax::CommandPtr& command) {
  reap_nonblocking();
  if (command == nullptr) return 2;

  std::vector<int> saved_redirs;
  if (!command->redirections.empty()) {
    (void)apply_parent_redirections(command->redirections, saved_redirs);
  }

  int status = 0;
  switch (command->kind) {
    case syntax::CommandKind::Sequence:
      status = execute_sequence(command);
      break;
    case syntax::CommandKind::AndOr:
      status = execute_and_or(command);
      break;
    case syntax::CommandKind::Group:
      status = command->children.empty() ? 0 : execute_sequence(command);
      break;
    case syntax::CommandKind::Subshell:
      status = execute_subshell(command);
      break;
    case syntax::CommandKind::If:
      status = execute_if(command);
      break;
    case syntax::CommandKind::For:
      status = execute_for(command);
      break;
    case syntax::CommandKind::While:
    case syntax::CommandKind::Until:
      status = execute_while(command);
      break;
    case syntax::CommandKind::Case:
      status = execute_case(command);
      break;
    case syntax::CommandKind::CondExpr:
      status = execute_cond(command->cond_expr);
      break;
    case syntax::CommandKind::ArithCommand:
      status = execute_arith_command(command);
      break;
    case syntax::CommandKind::Time:
      status = execute_time(command);
      break;
    case syntax::CommandKind::Function:
      if (!command->children.empty()) {
        state_.functions[command->name] = command->children.front();
      }
      status = 0;
      break;
    default: {
      const auto plan = planner_.build(command);
      status = execute_plan(plan);
      if (plan.negated) status = status == 0 ? 1 : 0;
      break;
    }
  }

  if (!saved_redirs.empty()) {
    restore_parent_descriptors(saved_redirs);
  }

  state_.last_status = status;
  return status;
}

int Runtime::execute(const syntax::Program& program) {
  int status = 0;
  for (const auto& command : program.commands) {
    status = execute_command(command);
    if (exit_requested_) break;
  }
  state_.last_status = status;
  reap_nonblocking();
  return status;
}

int Runtime::execute_string(const std::string& script) {
  source::SourceBuffer source(script, "<eval>");
  syntax::Lexer lexer(source);
  auto lexed = lexer.run();
  syntax::Parser parser(source, std::move(lexed.tokens));
  auto parsed = parser.run();
  if (lexed.diagnostics.has_error() || parsed.diagnostics.has_error()) return 2;
  return execute(parsed.program);
}

std::string Runtime::substitute(const std::string& source_text, int& status) {
  int output_pipe[2];
  if (pipe(output_pipe) != 0) {
    status = 125;
    return {};
  }
  const pid_t pid = fork();
  if (pid == 0) {
    close(output_pipe[0]);
    dup2(output_pipe[1], STDOUT_FILENO);
    close(output_pipe[1]);
    source::SourceBuffer source(source_text, "<command-substitution>");
    syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    syntax::Parser parser(source, std::move(lexed.tokens));
    auto parsed = parser.run();
    if (lexed.diagnostics.has_error() || parsed.diagnostics.has_error()) _exit(2);
    expand::ShellState child_state = state_;
    Runtime child_runtime(child_state);
    _exit(child_runtime.execute(parsed.program));
  }
  close(output_pipe[1]);
  std::string result;
  std::array<char, 4096> buffer{};
  while (true) {
    const ssize_t count = read(output_pipe[0], buffer.data(), buffer.size());
    if (count <= 0) break;
    result.append(buffer.data(), static_cast<std::size_t>(count));
  }
  close(output_pipe[0]);
  int child_status = 0;
  while (waitpid(pid, &child_status, 0) < 0 && errno == EINTR) {}
  status = decode_status(child_status);
  while (!result.empty() && result.back() == '\n') result.pop_back();
  return result;
}

}  // namespace splice::runtime
