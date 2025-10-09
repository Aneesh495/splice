#include "builtins/builtins.hpp"

#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>

namespace splice::builtins {
namespace {

void write_all(int fd, std::string_view value) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const ssize_t written = ::write(fd, value.data() + offset, value.size() - offset);
    if (written <= 0) return;
    offset += static_cast<std::size_t>(written);
  }
}

void error(Context& context, std::string_view value) {
  write_all(context.error_fd, std::string("splice: ") + std::string(value) + "\n");
}

bool number(std::string_view value, int& output, int base = 10) {
  if (value.empty()) return false;
  const auto begin = value.data();
  const auto end = begin + value.size();
  const auto result = std::from_chars(begin, end, output, base);
  return result.ec == std::errc{} && result.ptr == end;
}

std::string unescape_printf(std::string_view value) {
  std::string result;
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '\\' || index + 1 >= value.size()) {
      result.push_back(value[index]);
      continue;
    }
    const char escaped = value[++index];
    switch (escaped) {
      case 'n': result.push_back('\n'); break;
      case 't': result.push_back('\t'); break;
      case 'r': result.push_back('\r'); break;
      case 'b': result.push_back('\b'); break;
      case 'f': result.push_back('\f'); break;
      case 'v': result.push_back('\v'); break;
      case '\\': result.push_back('\\'); break;
      case '0': {
        int value_number = 0;
        std::size_t consumed = 0;
        while (index + 1 < value.size() && consumed < 3 && value[index + 1] >= '0' && value[index + 1] <= '7') {
          value_number = value_number * 8 + value[++index] - '0';
          ++consumed;
        }
        result.push_back(static_cast<char>(value_number));
        break;
      }
      default: result.push_back(escaped); break;
    }
  }
  return result;
}

int run_printf(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() < 2) return 0;
  const std::string format = argv[1];
  std::size_t argument = 2;
  do {
    std::string output;
    bool consumed_argument = false;
    for (std::size_t index = 0; index < format.size(); ++index) {
      if (format[index] != '%') {
        output.push_back(format[index]);
        continue;
      }
      if (index + 1 >= format.size()) {
        output.push_back('%');
        break;
      }
      const char specifier = format[++index];
      if (specifier == '%') {
        output.push_back('%');
      } else if (specifier == 's') {
        if (argument < argv.size()) output += argv[argument++];
        consumed_argument = true;
      } else if (specifier == 'd' || specifier == 'i') {
        int value = 0;
        if (argument < argv.size()) number(argv[argument++], value);
        consumed_argument = true;
        output += std::to_string(value);
      } else if (specifier == 'b') {
        if (argument < argv.size()) output += unescape_printf(argv[argument++]);
        consumed_argument = true;
      } else {
        error(context, "printf: unsupported format specifier");
        return 2;
      }
    }
    write_all(context.output_fd, unescape_printf(output));
    if (!consumed_argument || argument >= argv.size()) break;
  } while (true);
  return 0;
}

int run_export(const std::vector<std::string>& argv, Context& context) {
  for (std::size_t index = 1; index < argv.size(); ++index) {
    const std::string& value = argv[index];
    const std::size_t separator = value.find('=');
    const std::string name = value.substr(0, separator);
    if (!expand::valid_name(name)) {
      error(context, "export: invalid variable name");
      return 2;
    }
    if (separator == std::string::npos) {
      if (!context.state.mark_exported(name)) return 1;
    } else if (!context.state.set(name, value.substr(separator + 1), true)) {
      error(context, "export: readonly variable");
      return 1;
    }
  }
  if (argv.size() == 1) {
    for (const auto& value : context.state.environment()) write_all(context.output_fd, value + "\n");
  }
  return 0;
}

int run_set(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() == 1) {
    for (const auto& value : context.state.environment()) write_all(context.output_fd, value + "\n");
    return 0;
  }
  for (std::size_t index = 1; index < argv.size(); ++index) {
    const std::string_view option(argv[index]);
    if (option == "--") {
      context.state.positional.assign(argv.begin() + static_cast<std::ptrdiff_t>(index + 1), argv.end());
      break;
    }
    const bool enable = option.starts_with("-");
    if ((!enable && !option.starts_with("+")) || option.size() < 2) {
      context.state.positional.assign(argv.begin() + static_cast<std::ptrdiff_t>(index), argv.end());
      break;
    }
    if (option == "-e" || option == "+e") context.state.set_option(expand::ShellOption::Errexit, enable);
    else if (option == "-u" || option == "+u") context.state.set_option(expand::ShellOption::Nounset, enable);
    else if (option == "-C" || option == "+C") context.state.set_option(expand::ShellOption::Noclobber, enable);
    else if (option == "-o" || option == "+o") {
      if (index + 1 >= argv.size()) return 2;
      const std::string_view name(argv[++index]);
      if (name == "pipefail") context.state.set_option(expand::ShellOption::Pipefail, enable);
      else {
        error(context, "set: unsupported option");
        return 2;
      }
    } else {
      error(context, "set: unsupported option");
      return 2;
    }
  }
  return 0;
}

}  // namespace

std::vector<std::string> names() {
  return {":", "[", "bg", "break", "cd", "command", "continue", "echo", "eval", "exec", "exit",
          "export", "false", "hash", "help", "jobs", "kill", "printf", "pwd", "read", "readonly",
          "return", "set", "shift", "test", "times", "trap", "true", "type", "umask", "ulimit", "unset", "wait"};
}

bool is_builtin(std::string_view name) noexcept {
  static const auto builtin_names = names();
  return std::find(builtin_names.begin(), builtin_names.end(), name) != builtin_names.end();
}

Result run(const std::vector<std::string>& argv, Context& context) {
  if (argv.empty()) return Result{true, 0, false};
  const std::string_view name(argv.front());
  if (!is_builtin(name)) return Result{};
  Result result{true, 0, false};
  if (name == ":" || name == "true") return result;
  if (name == "false") {
    result.status = 1;
    return result;
  }
  if (name == "echo") {
    bool newline = true;
    std::size_t first = 1;
    if (first < argv.size() && argv[first] == "-n") {
      newline = false;
      ++first;
    }
    std::string output;
    for (std::size_t index = first; index < argv.size(); ++index) {
      if (index != first) output.push_back(' ');
      output += argv[index];
    }
    if (newline) output.push_back('\n');
    write_all(context.output_fd, output);
  } else if (name == "printf") {
    result.status = run_printf(argv, context);
  } else if (name == "pwd") {
    char buffer[4096];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) result.status = 1;
    else write_all(context.output_fd, std::string(buffer) + "\n");
  } else if (name == "cd") {
    const std::string destination = argv.size() < 2 ? context.state.value("HOME") : argv[1];
    char old[4096];
    if (getcwd(old, sizeof(old)) == nullptr || chdir(destination.c_str()) != 0) {
      error(context, std::string("cd: ") + std::strerror(errno));
      result.status = 1;
    } else {
      char current[4096];
      if (getcwd(current, sizeof(current)) != nullptr) {
        context.state.set("OLDPWD", old, context.state.is_exported("OLDPWD"));
        context.state.set("PWD", current, context.state.is_exported("PWD"));
        context.state.current_directory = current;
      }
    }
  } else if (name == "export") {
    result.status = run_export(argv, context);
  } else if (name == "readonly") {
    for (std::size_t index = 1; index < argv.size(); ++index) {
      if (!context.state.mark_readonly(argv[index])) result.status = 1;
    }
  } else if (name == "unset") {
    for (std::size_t index = 1; index < argv.size(); ++index) {
      if (!context.state.unset(argv[index])) result.status = 1;
    }
  } else if (name == "set") {
    result.status = run_set(argv, context);
  } else if (name == "shift") {
    int count = 1;
    if (argv.size() > 1 && !number(argv[1], count)) result.status = 2;
    if (result.status == 0 && count >= 0 && static_cast<std::size_t>(count) <= context.state.positional.size()) {
      context.state.positional.erase(context.state.positional.begin(), context.state.positional.begin() + count);
    } else if (result.status == 0) result.status = 1;
  } else if (name == "command" && argv.size() > 1 && context.nested) {
    std::vector<std::string> nested(argv.begin() + 1, argv.end());
    result.status = context.nested(nested);
  } else if (name == "type") {
    for (std::size_t index = 1; index < argv.size(); ++index) {
      if (is_builtin(argv[index])) write_all(context.output_fd, argv[index] + " is a shell builtin\n");
      else write_all(context.output_fd, argv[index] + " is an external command\n");
    }
  } else if (name == "hash") {
    result.status = 0;
  } else if (name == "umask") {
    if (argv.size() == 1) {
      const mode_t old = umask(0);
      umask(old);
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%04o\n", old);
      write_all(context.output_fd, buffer);
    } else {
      int value = 0;
      if (!number(argv[1], value, 8)) result.status = 2;
      else umask(static_cast<mode_t>(value));
    }
  } else if (name == "read") {
    std::string line;
    char character = '\0';
    bool reached_eof = false;
    while (true) {
      const ssize_t count = ::read(context.input_fd, &character, 1);
      if (count <= 0) {
        reached_eof = true;
        break;
      }
      if (character == '\n') break;
      line.push_back(character);
    }
    if (argv.size() > 1) context.state.set(argv[1], line);
    else context.state.set("REPLY", line);
    if (line.empty() && reached_eof) result.status = 1;
  } else if (name == "help") {
    write_all(context.output_fd, "Splice builtins: cd pwd echo printf export unset readonly set shift read command type hash umask true false jobs wait kill exit\n");
  } else if (name == "exit") {
    if (argv.size() > 1 && !number(argv[1], result.status)) result.status = 2;
    result.request_exit = context.parent;
  } else if (name == "kill") {
    int signal_number = SIGTERM;
    std::size_t first = 1;
    if (first < argv.size() && argv[first].starts_with("-")) {
      if (!number(argv[first].substr(1), signal_number)) result.status = 2;
      ++first;
    }
    for (; result.status == 0 && first < argv.size(); ++first) {
      int pid = 0;
      if (!number(argv[first], pid) || ::kill(static_cast<pid_t>(pid), signal_number) != 0) result.status = 1;
    }
  } else if (name == "jobs" || name == "wait" || name == "bg") {
    error(context, std::string(name) + ": job control is provided by the runtime");
    result.status = 1;
  } else if (name == "exec") {
    error(context, "exec: replacing the shell is not available in this execution context");
    result.status = 125;
  } else {
    error(context, std::string(name) + ": builtin is recognized but not implemented");
    result.status = 2;
  }
  context.state.last_status = result.status;
  return result;
}

}  // namespace splice::builtins
