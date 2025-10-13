#include "runtime/runtime.hpp"

#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace splice::runtime {

int Runtime::signal_write_fd_ = -1;namespace {

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
    if (job.state == JobState::Running) {
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
      if (dup2(fd, action.fd) < 0) {
        const int saved = errno;
        close_if_open(fd);
        errno = saved;
        return -kErrorDup;
      }
      close_if_open(fd);
    } else if (action.kind == plan::DescriptorActionKind::Duplicate) {
      if (dup2(action.target_fd, action.fd) < 0) return -kErrorDup;
    } else if (action.kind == plan::DescriptorActionKind::Close) {
      if (close(action.fd) < 0 && errno != EBADF) return -kErrorDup;
    } else if (action.kind == plan::DescriptorActionKind::HereDocument) {
      int pipe_fds[2];
      if (pipe(pipe_fds) != 0) return -kErrorOpen;
      const std::string& body = action.body;
      const ssize_t ignored = write(pipe_fds[1], body.data(), body.size());
      (void)ignored;
      close(pipe_fds[1]);
      if (dup2(pipe_fds[0], action.fd) < 0) {
        close(pipe_fds[0]);
        return -kErrorDup;
      }
      close(pipe_fds[0]);
    }
  }
  return 0;
}

int Runtime::apply_parent_descriptors(const plan::PlannedCommand& command, std::vector<int>& saved) {
  saved.assign(3, -1);
  for (int fd = 0; fd < 3; ++fd) {
    saved[static_cast<std::size_t>(fd)] = dup(fd);
  }
  return apply_descriptor_actions(command);
}

void Runtime::restore_parent_descriptors(const std::vector<int>& saved) {
  for (int fd = 0; fd < 3 && static_cast<std::size_t>(fd) < saved.size(); ++fd) {
    if (saved[static_cast<std::size_t>(fd)] >= 0) {
      dup2(saved[static_cast<std::size_t>(fd)], fd);
      close(saved[static_cast<std::size_t>(fd)]);
    }
  }
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
  close_if_open(output_fd == STDOUT_FILENO ? -1 : output_fd);
  close_if_open(error_fd == STDERR_FILENO ? -1 : error_fd);
  if (apply_descriptor_actions(command) != 0) {
    write_child_error(error_pipe, kErrorOpen, errno);
    _exit(125);
  }
  for (const auto& [name, value] : command.assignments) setenv(name.c_str(), value.c_str(), 1);
  if (command.argv.empty()) _exit(0);
  if (builtins::is_builtin(command.argv.front())) {
    builtins::Context context{state_, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, false, {}, {}};
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
    if (!state_.set(name, value)) {
      restore_parent_descriptors(saved);
      report_error("assignment failed for readonly variable");
      return 1;
    }
  }
  builtins::Context context{state_, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, true,
                            [this](const std::vector<std::string>& argv) {
                              plan::PlannedCommand nested;
                              nested.argv = argv;
                              builtins::Context nested_context{state_, 0, 1, 2, true, {}, {}};
                              return builtins::run(argv, nested_context).status;
                            },
                            [this](const std::vector<std::string>& argv) {
                              return job_control(argv);
                            }};
  const auto result = builtins::run(command.argv, context);
  restore_parent_descriptors(saved);
  if (result.request_exit) exit_requested_ = true;
  return result.status;
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
    while (job.results.size() < job.processes.size()) {
      int status = 0;
      const pid_t pid = waitpid(-1, &status, 0);
      if (pid < 0) {
        if (errno == EINTR) continue;
        break;
      }
      reap_one(pid, status);
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
  job->results.push_back(ProcessResult{ProcessId{pid}, decode_status(status), WIFEXITED(status), WIFSIGNALED(status)});
  if (trace_ != nullptr) (void)trace_->record("child-status", {{"pid", std::to_string(pid)}, {"status", std::to_string(decode_status(status))}});
  bool all_done = job->results.size() == job->processes.size();
  if (WIFSTOPPED(status)) job->state = JobState::Stopped;
  else if (all_done) {
    job->state = JobState::Completed;
    job->status = job->results.back().status;
    if (state_.option(expand::ShellOption::Pipefail)) {
      for (const auto& result : job->results) if (result.status != 0) job->status = result.status;
    }
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

int Runtime::launch_pipeline(const plan::ExecutionPlan& plan) {
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
    for (auto& pair : pipes) {
      if (pipe(pair.data()) != 0) {
        report_error(std::string("pipe: ") + std::strerror(errno));
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
    unblock_signals();
    return 125;
  }
  error_read = error_pipe[0];
  error_write = error_pipe[1];
  set_cloexec(error_write);
  Job job;
  job.id = next_job_id_++;
  job.background = plan.background;
  pid_t group = -1;
  for (std::size_t index = 0; index < plan.stages.size(); ++index) {
    const int input = index == 0 ? STDIN_FILENO : pipes[index - 1][0];
    const int output = index + 1 == plan.stages.size() ? STDOUT_FILENO : pipes[index][1];
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
      (void)child_setup_and_exec(plan.stages[index], input, output, STDERR_FILENO, error_write);
      _exit(125);
    }
    if (pid < 0) {
      report_error(std::string("fork: ") + std::strerror(errno));
      for (const auto process : job.processes) kill(process.value, SIGTERM);
      close(error_read);
      close(error_write);
      unblock_signals();
      return 125;
    }
    if (group < 0) group = pid;
    setpgid(pid, group);
    job.processes.push_back(ProcessId{pid});
    if (index == 0) job.process_group = group;
  }
  for (auto& pair : pipes) {
    close(pair[0]);
    close(pair[1]);
  }
  close(error_write);
  job.process_group = group;
  register_job(job);
  if (trace_ != nullptr) (void)trace_->record("pipeline-registered", {{"job", std::to_string(job.id)}, {"pgid", std::to_string(group)}, {"stages", std::to_string(job.processes.size())}});
  unblock_signals();
  Job& stored = jobs_.at(job.id);
  if (!plan.background && interactive_ && group > 0) tcsetpgrp(shell_terminal_, group);
  if (plan.background) {
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
  if (plan.stages.size() == 1 && !plan.background &&
      (plan.stages.front().argv.empty() || builtins::is_builtin(plan.stages.front().argv.front()))) {
    return execute_parent_builtin(plan.stages.front());
  }
  return launch_pipeline(plan);
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
    if (exit_requested_) break;
    if (state_.option(expand::ShellOption::Errexit) && status != 0) break;
  }
  return status;
}

int Runtime::execute_command(const syntax::CommandPtr& command) {
  reap_nonblocking();
  if (command == nullptr) return 2;
  if (command->kind == syntax::CommandKind::Sequence) return execute_sequence(command);
  if (command->kind == syntax::CommandKind::AndOr) return execute_and_or(command);
  const auto plan = planner_.build(command);
  int status = execute_plan(plan);
  if (plan.negated) status = status == 0 ? 1 : 0;
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
