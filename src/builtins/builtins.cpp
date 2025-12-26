#include "builtins/builtins.hpp"

#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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

bool number_ll(std::string_view value, long long& output, int base = 10) {
  if (value.empty()) return false;
  const auto begin = value.data();
  const auto end = begin + value.size();
  const auto result = std::from_chars(begin, end, output, base);
  return result.ec == std::errc{} && result.ptr == end;
}

std::string shell_escape(std::string_view value) {
  if (value.empty()) return "''";
  bool safe = true;
  for (char ch : value) {
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-' && ch != '.' && ch != '/') {
      safe = false;
      break;
    }
  }
  if (safe) return std::string(value);
  std::string result = "'";
  for (char ch : value) {
    if (ch == '\'') result += "'\\''";
    else result.push_back(ch);
  }
  result += "'";
  return result;
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
      case 'a': result.push_back('\a'); break;
      case 'b': result.push_back('\b'); break;
      case 'e':
      case 'E': result.push_back('\033'); break;
      case 'f': result.push_back('\f'); break;
      case 'n': result.push_back('\n'); break;
      case 'r': result.push_back('\r'); break;
      case 't': result.push_back('\t'); break;
      case 'v': result.push_back('\v'); break;
      case '\\': result.push_back('\\'); break;
      case '\'': result.push_back('\''); break;
      case '"': result.push_back('"'); break;
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
      case 'x': {
        int value_number = 0;
        std::size_t consumed = 0;
        while (index + 1 < value.size() && consumed < 2 && std::isxdigit(static_cast<unsigned char>(value[index + 1]))) {
          char c = value[++index];
          int d = (c >= '0' && c <= '9') ? (c - '0') : ((c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10));
          value_number = value_number * 16 + d;
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
        if (format[index] == '\\' && index + 1 < format.size()) {
          output += unescape_printf(std::string_view(format).substr(index, 2));
          ++index;
        } else {
          output.push_back(format[index]);
        }
        continue;
      }
      if (index + 1 >= format.size()) {
        output.push_back('%');
        break;
      }
      if (format[index + 1] == '%') {
        output.push_back('%');
        ++index;
        continue;
      }

      std::string flags;
      int width = -1;
      int precision = -1;
      ++index;

      while (index < format.size() && (format[index] == '-' || format[index] == '+' || format[index] == ' ' || format[index] == '#' || format[index] == '0')) {
        flags.push_back(format[index++]);
      }
      if (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index]))) {
        std::size_t w_start = index;
        while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
        number(std::string_view(format).substr(w_start, index - w_start), width);
      }
      if (index < format.size() && format[index] == '.') {
        ++index;
        std::size_t p_start = index;
        while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
        if (index > p_start) {
          number(std::string_view(format).substr(p_start, index - p_start), precision);
        } else {
          precision = 0;
        }
      }

      if (index >= format.size()) {
        output.push_back('%');
        break;
      }

      const char specifier = format[index];
      if (specifier == 's') {
        std::string val = argument < argv.size() ? argv[argument++] : std::string{};
        consumed_argument = true;
        if (precision >= 0 && static_cast<std::size_t>(precision) < val.size()) {
          val = val.substr(0, static_cast<std::size_t>(precision));
        }
        if (width > 0 && static_cast<int>(val.size()) < width) {
          const std::size_t pad = static_cast<std::size_t>(width) - val.size();
          if (flags.find('-') != std::string::npos) val.append(pad, ' ');
          else val.insert(0, pad, ' ');
        }
        output += val;
      } else if (specifier == 'q') {
        std::string val = argument < argv.size() ? argv[argument++] : std::string{};
        consumed_argument = true;
        output += shell_escape(val);
      } else if (specifier == 'b') {
        std::string val = argument < argv.size() ? argv[argument++] : std::string{};
        consumed_argument = true;
        output += unescape_printf(val);
      } else if (specifier == 'c') {
        std::string val = argument < argv.size() ? argv[argument++] : std::string{};
        consumed_argument = true;
        output.push_back(val.empty() ? '\0' : val.front());
      } else if (specifier == 'd' || specifier == 'i') {
        long long val = 0;
        if (argument < argv.size()) number_ll(argv[argument++], val);
        consumed_argument = true;
        std::string val_str = std::to_string(val);
        if (width > 0 && static_cast<int>(val_str.size()) < width) {
          const std::size_t pad = static_cast<std::size_t>(width) - val_str.size();
          if (flags.find('0') != std::string::npos) val_str.insert(val < 0 ? 1 : 0, pad, '0');
          else if (flags.find('-') != std::string::npos) val_str.append(pad, ' ');
          else val_str.insert(0, pad, ' ');
        }
        output += val_str;
      } else if (specifier == 'u' || specifier == 'o' || specifier == 'x' || specifier == 'X') {
        unsigned long long val = 0;
        if (argument < argv.size()) {
          long long signed_val = 0;
          number_ll(argv[argument++], signed_val);
          val = static_cast<unsigned long long>(signed_val);
        }
        consumed_argument = true;
        std::ostringstream ss;
        if (specifier == 'u') ss << val;
        else if (specifier == 'o') ss << std::oct << val;
        else if (specifier == 'x') ss << std::hex << val;
        else if (specifier == 'X') ss << std::hex << std::uppercase << val;
        output += ss.str();
      } else {
        error(context, "printf: unsupported format specifier");
        return 2;
      }
    }
    write_all(context.output_fd, output);
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
      else if (name == "errexit") context.state.set_option(expand::ShellOption::Errexit, enable);
      else if (name == "nounset") context.state.set_option(expand::ShellOption::Nounset, enable);
      else if (name == "noclobber") context.state.set_option(expand::ShellOption::Noclobber, enable);
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

bool eval_unary_test(std::string_view op, std::string_view target, Context& context) {
  struct stat st{};
  const std::string path(target);
  if (op == "-b") return stat(path.c_str(), &st) == 0 && S_ISBLK(st.st_mode);
  if (op == "-c") return stat(path.c_str(), &st) == 0 && S_ISCHR(st.st_mode);
  if (op == "-d") return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
  if (op == "-e") return stat(path.c_str(), &st) == 0;
  if (op == "-f") return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
  if (op == "-g") return stat(path.c_str(), &st) == 0 && (st.st_mode & S_ISGID) != 0;
  if (op == "-h" || op == "-L") return lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
  if (op == "-k") return stat(path.c_str(), &st) == 0 && (st.st_mode & S_ISVTX) != 0;
  if (op == "-p") return stat(path.c_str(), &st) == 0 && S_ISFIFO(st.st_mode);
  if (op == "-r") return access(path.c_str(), R_OK) == 0;
  if (op == "-s") return stat(path.c_str(), &st) == 0 && st.st_size > 0;
  if (op == "-S") return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
  if (op == "-t") {
    int fd = 0;
    if (number(target, fd)) return isatty(fd) != 0;
    return false;
  }
  if (op == "-u") return stat(path.c_str(), &st) == 0 && (st.st_mode & S_ISUID) != 0;
  if (op == "-w") return access(path.c_str(), W_OK) == 0;
  if (op == "-x") return access(path.c_str(), X_OK) == 0;
  if (op == "-O") return stat(path.c_str(), &st) == 0 && st.st_uid == geteuid();
  if (op == "-G") return stat(path.c_str(), &st) == 0 && st.st_gid == getegid();
  if (op == "-z") return target.empty();
  if (op == "-n") return !target.empty();
  if (op == "-v") return context.state.is_set(target);
  if (op == "-o") {
    if (target == "errexit") return context.state.option(expand::ShellOption::Errexit);
    if (target == "nounset") return context.state.option(expand::ShellOption::Nounset);
    if (target == "noclobber") return context.state.option(expand::ShellOption::Noclobber);
    if (target == "pipefail") return context.state.option(expand::ShellOption::Pipefail);
    return false;
  }
  return false;
}

bool eval_binary_test(std::string_view left, std::string_view op, std::string_view right) {
  if (op == "=" || op == "==") return left == right;
  if (op == "!=") return left != right;
  if (op == "<") return left < right;
  if (op == ">") return left > right;

  long long num_left = 0;
  long long num_right = 0;
  const bool has_left = number_ll(left, num_left);
  const bool has_right = number_ll(right, num_right);

  if (op == "-eq") return has_left && has_right && num_left == num_right;
  if (op == "-ne") return has_left && has_right && num_left != num_right;
  if (op == "-lt") return has_left && has_right && num_left < num_right;
  if (op == "-le") return has_left && has_right && num_left <= num_right;
  if (op == "-gt") return has_left && has_right && num_left > num_right;
  if (op == "-ge") return has_left && has_right && num_left >= num_right;

  struct stat st_left{};
  struct stat st_right{};
  const bool left_stat = stat(std::string(left).c_str(), &st_left) == 0;
  const bool right_stat = stat(std::string(right).c_str(), &st_right) == 0;

  if (op == "-nt") return left_stat && (!right_stat || st_left.st_mtime > st_right.st_mtime);
  if (op == "-ot") return right_stat && (!left_stat || st_left.st_mtime < st_right.st_mtime);
  if (op == "-ef") return left_stat && right_stat && st_left.st_dev == st_right.st_dev && st_left.st_ino == st_right.st_ino;

  return false;
}

class TestParser {
 public:
  TestParser(const std::vector<std::string>& tokens, std::size_t start, std::size_t end, Context& context)
      : tokens_(tokens), index_(start), end_(end), context_(context) {}

  bool parse() {
    bool result = parse_or();
    return result;
  }

 private:
  bool parse_or() {
    bool val = parse_and();
    while (index_ < end_ && tokens_[index_] == "-o") {
      ++index_;
      const bool right = parse_and();
      val = val || right;
    }
    return val;
  }

  bool parse_and() {
    bool val = parse_primary();
    while (index_ < end_ && tokens_[index_] == "-a") {
      ++index_;
      const bool right = parse_primary();
      val = val && right;
    }
    return val;
  }

  bool parse_primary() {
    if (index_ >= end_) return false;
    if (tokens_[index_] == "!") {
      ++index_;
      return !parse_primary();
    }
    if (tokens_[index_] == "(") {
      ++index_;
      bool val = parse_or();
      if (index_ < end_ && tokens_[index_] == ")") ++index_;
      return val;
    }
    if (tokens_[index_].starts_with("-") && tokens_[index_].size() == 2 && index_ + 1 < end_) {
      const std::string op = tokens_[index_++];
      const std::string target = tokens_[index_++];
      return eval_unary_test(op, target, context_);
    }
    if (index_ + 2 < end_) {
      const std::string_view candidate_op = tokens_[index_ + 1];
      if (candidate_op == "=" || candidate_op == "==" || candidate_op == "!=" ||
          candidate_op == "<" || candidate_op == ">" ||
          candidate_op == "-eq" || candidate_op == "-ne" ||
          candidate_op == "-lt" || candidate_op == "-le" ||
          candidate_op == "-gt" || candidate_op == "-ge" ||
          candidate_op == "-nt" || candidate_op == "-ot" || candidate_op == "-ef") {
        const std::string left = tokens_[index_];
        const std::string op = tokens_[index_ + 1];
        const std::string right = tokens_[index_ + 2];
        index_ += 3;
        return eval_binary_test(left, op, right);
      }
    }
    const std::string str = tokens_[index_++];
    return !str.empty();
  }

  const std::vector<std::string>& tokens_;
  std::size_t index_;
  std::size_t end_;
  Context& context_;
};

int run_test(const std::vector<std::string>& argv, Context& context) {
  std::size_t start = 1;
  std::size_t end = argv.size();
  if (argv.front() == "[") {
    if (argv.size() < 2 || argv.back() != "]") {
      error(context, "[: missing `]'");
      return 2;
    }
    end = argv.size() - 1;
  }

  const std::size_t count = end - start;
  if (count == 0) return 1;
  if (count == 1) return argv[start].empty() ? 1 : 0;
  if (count == 2) {
    if (argv[start] == "!") return argv[start + 1].empty() ? 0 : 1;
    if (argv[start].starts_with("-")) {
      return eval_unary_test(argv[start], argv[start + 1], context) ? 0 : 1;
    }
  }
  if (count == 3) {
    const std::string_view op = argv[start + 1];
    if (op == "=" || op == "==" || op == "!=" || op == "<" || op == ">" ||
        op == "-eq" || op == "-ne" || op == "-lt" || op == "-le" ||
        op == "-gt" || op == "-ge" || op == "-nt" || op == "-ot" || op == "-ef") {
      return eval_binary_test(argv[start], op, argv[start + 2]) ? 0 : 1;
    }
    if (argv[start] == "!") {
      return eval_unary_test(argv[start + 1], argv[start + 2], context) ? 1 : 0;
    }
  }

  TestParser parser(argv, start, end, context);
  return parser.parse() ? 0 : 1;
}

int run_read(const std::vector<std::string>& argv, Context& context) {
  bool raw = false;
  std::string prompt;
  std::string array_name;
  char delimiter = '\n';
  int max_chars = -1;
  bool silent = false;

  std::size_t index = 1;
  while (index < argv.size() && argv[index].starts_with("-") && argv[index].size() > 1 && argv[index] != "--") {
    const std::string_view opt = argv[index++];
    for (std::size_t c = 1; c < opt.size(); ++c) {
      if (opt[c] == 'r') raw = true;
      else if (opt[c] == 's') silent = true;
      else if (opt[c] == 'p') {
        if (index < argv.size()) prompt = argv[index++];
        break;
      } else if (opt[c] == 'a') {
        if (index < argv.size()) array_name = argv[index++];
        break;
      } else if (opt[c] == 'd') {
        if (index < argv.size()) delimiter = argv[index++].front();
        break;
      } else if (opt[c] == 'n') {
        if (index < argv.size()) number(argv[index++], max_chars);
        break;
      }
    }
  }
  if (index < argv.size() && argv[index] == "--") ++index;

  if (!prompt.empty()) {
    write_all(context.error_fd, prompt);
  }

  struct termios old_term{};
  bool altered_term = false;
  if (silent && isatty(context.input_fd)) {
    if (tcgetattr(context.input_fd, &old_term) == 0) {
      struct termios new_term = old_term;
      new_term.c_lflag &= static_cast<unsigned long>(~ECHO);
      tcsetattr(context.input_fd, TCSANOW, &new_term);
      altered_term = true;
    }
  }

  std::string line;
  char ch = '\0';
  bool reached_eof = false;
  int chars_read = 0;
  while (max_chars < 0 || chars_read < max_chars) {
    const ssize_t count = ::read(context.input_fd, &ch, 1);
    if (count <= 0) {
      reached_eof = true;
      break;
    }
    ++chars_read;
    if (ch == delimiter) break;
    if (!raw && ch == '\\') {
      char next_ch = '\0';
      const ssize_t next_count = ::read(context.input_fd, &next_ch, 1);
      if (next_count > 0 && next_ch != '\n') {
        line.push_back(next_ch);
        continue;
      }
    }
    line.push_back(ch);
  }

  if (altered_term) {
    tcsetattr(context.input_fd, TCSANOW, &old_term);
    if (silent) write_all(context.output_fd, "\n");
  }

  if (!array_name.empty()) {
    std::vector<std::string> words;
    std::string current;
    for (char c : line) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        if (!current.empty()) {
          words.push_back(std::move(current));
          current.clear();
        }
      } else {
        current.push_back(c);
      }
    }
    if (!current.empty()) words.push_back(std::move(current));
    context.state.set_array(array_name, words);
    return (line.empty() && reached_eof) ? 1 : 0;
  }

  std::vector<std::string> var_names;
  for (; index < argv.size(); ++index) var_names.push_back(argv[index]);
  if (var_names.empty()) var_names.push_back("REPLY");

  if (var_names.size() == 1) {
    context.state.set(var_names.front(), line);
  } else {
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < var_names.size(); ++i) {
      while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
      if (i + 1 == var_names.size()) {
        context.state.set(var_names[i], line.substr(cursor));
      } else {
        std::size_t end_word = cursor;
        while (end_word < line.size() && !std::isspace(static_cast<unsigned char>(line[end_word]))) ++end_word;
        context.state.set(var_names[i], line.substr(cursor, end_word - cursor));
        cursor = end_word;
      }
    }
  }

  return (line.empty() && reached_eof) ? 1 : 0;
}

int run_mapfile(const std::vector<std::string>& argv, Context& context) {
  bool strip_newline = false;
  int count = -1;
  int skip = 0;
  int origin = 0;
  std::string target = "MAPFILE";

  std::size_t index = 1;
  while (index < argv.size() && argv[index].starts_with("-")) {
    const std::string_view opt = argv[index++];
    if (opt == "-t") strip_newline = true;
    else if (opt == "-n" && index < argv.size()) number(argv[index++], count);
    else if (opt == "-s" && index < argv.size()) number(argv[index++], skip);
    else if (opt == "-O" && index < argv.size()) number(argv[index++], origin);
  }
  if (index < argv.size()) target = argv[index];

  std::vector<std::string> lines;
  std::string line;
  char ch = '\0';
  int skipped = 0;
  int read_count = 0;

  while (count < 0 || read_count < count) {
    const ssize_t n = ::read(context.input_fd, &ch, 1);
    if (n <= 0) {
      if (!line.empty()) {
        if (skipped >= skip) {
          lines.push_back(line);
          ++read_count;
        }
      }
      break;
    }
    if (ch == '\n') {
      if (skipped >= skip) {
        if (!strip_newline) line.push_back('\n');
        lines.push_back(line);
        ++read_count;
      } else {
        ++skipped;
      }
      line.clear();
    } else {
      line.push_back(ch);
    }
  }

  for (std::size_t i = 0; i < lines.size(); ++i) {
    context.state.set_array_element(target, static_cast<std::size_t>(origin) + i, lines[i]);
  }
  return 0;
}

int run_dirs(const std::vector<std::string>& argv, Context& context) {
  (void)argv;
  std::string output = context.state.value("PWD");
  for (auto it = context.state.directory_stack.rbegin(); it != context.state.directory_stack.rend(); ++it) {
    output += " " + *it;
  }
  output += "\n";
  write_all(context.output_fd, output);
  return 0;
}

int run_pushd(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() < 2) {
    error(context, "pushd: no other directory");
    return 1;
  }
  const std::string current = context.state.value("PWD");
  const std::string dest = argv[1];
  if (chdir(dest.c_str()) != 0) {
    error(context, std::string("pushd: ") + std::strerror(errno));
    return 1;
  }
  context.state.directory_stack.push_back(current);
  char buf[4096];
  if (getcwd(buf, sizeof(buf)) != nullptr) {
    context.state.set("OLDPWD", current);
    context.state.set("PWD", buf);
    context.state.current_directory = buf;
  }
  return run_dirs(argv, context);
}

int run_popd(const std::vector<std::string>& argv, Context& context) {
  if (context.state.directory_stack.empty()) {
    error(context, "popd: directory stack empty");
    return 1;
  }
  const std::string target = context.state.directory_stack.back();
  context.state.directory_stack.pop_back();
  if (chdir(target.c_str()) != 0) {
    error(context, std::string("popd: ") + std::strerror(errno));
    return 1;
  }
  char buf[4096];
  if (getcwd(buf, sizeof(buf)) != nullptr) {
    context.state.set("PWD", buf);
    context.state.current_directory = buf;
  }
  return run_dirs(argv, context);
}

int run_alias(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() == 1 || (argv.size() == 2 && argv[1] == "-p")) {
    for (const auto& [name, val] : context.state.aliases) {
      write_all(context.output_fd, "alias " + name + "='" + val + "'\n");
    }
    return 0;
  }
  for (std::size_t i = 1; i < argv.size(); ++i) {
    const std::string& arg = argv[i];
    const std::size_t eq = arg.find('=');
    if (eq != std::string::npos) {
      const std::string name = arg.substr(0, eq);
      const std::string val = arg.substr(eq + 1);
      context.state.aliases[name] = val;
    } else {
      const auto it = context.state.aliases.find(arg);
      if (it != context.state.aliases.end()) {
        write_all(context.output_fd, "alias " + it->first + "='" + it->second + "'\n");
      } else {
        error(context, "alias: " + arg + ": not found");
        return 1;
      }
    }
  }
  return 0;
}

int run_unalias(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() < 2) {
    error(context, "unalias: usage: unalias [-a] name [name ...]");
    return 2;
  }
  if (argv[1] == "-a") {
    context.state.aliases.clear();
    return 0;
  }
  for (std::size_t i = 1; i < argv.size(); ++i) {
    context.state.aliases.erase(argv[i]);
  }
  return 0;
}

int run_trap(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() == 1) {
    for (const auto& [sig, cmd] : context.state.traps) {
      write_all(context.output_fd, "trap -- '" + cmd + "' " + std::to_string(sig) + "\n");
    }
    return 0;
  }
  const std::string action = argv[1];
  for (std::size_t i = 2; i < argv.size(); ++i) {
    int sig = -1;
    const std::string_view sig_name(argv[i]);
    if (sig_name == "EXIT" || sig_name == "0") sig = 0;
    else if (sig_name == "HUP" || sig_name == "SIGHUP") sig = SIGHUP;
    else if (sig_name == "INT" || sig_name == "SIGINT") sig = SIGINT;
    else if (sig_name == "QUIT" || sig_name == "SIGQUIT") sig = SIGQUIT;
    else if (sig_name == "TERM" || sig_name == "SIGTERM") sig = SIGTERM;
    else if (sig_name == "WINCH" || sig_name == "SIGWINCH") sig = SIGWINCH;
    else number(sig_name, sig);

    if (sig >= 0) {
      if (action == "-") {
        context.state.traps.erase(sig);
      } else {
        context.state.traps[sig] = action;
      }
    }
  }
  return 0;
}

int run_ulimit(const std::vector<std::string>& argv, Context& context) {
  int resource = RLIMIT_CORE;
  bool all = false;
  for (std::size_t i = 1; i < argv.size(); ++i) {
    const std::string_view opt = argv[i];
    if (opt == "-a") all = true;
    else if (opt == "-c") resource = RLIMIT_CORE;
    else if (opt == "-d") resource = RLIMIT_DATA;
    else if (opt == "-f") resource = RLIMIT_FSIZE;
    else if (opt == "-n") resource = RLIMIT_NOFILE;
    else if (opt == "-s") resource = RLIMIT_STACK;
    else if (opt == "-t") resource = RLIMIT_CPU;
  }
  if (all) {
    struct rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    write_all(context.output_fd, "open files                      (-n) " + std::to_string(rl.rlim_cur) + "\n");
    getrlimit(RLIMIT_STACK, &rl);
    write_all(context.output_fd, "stack size              (kbytes, -s) " + std::to_string(rl.rlim_cur / 1024) + "\n");
    return 0;
  }
  struct rlimit rl{};
  if (getrlimit(resource, &rl) == 0) {
    if (rl.rlim_cur == RLIM_INFINITY) write_all(context.output_fd, "unlimited\n");
    else write_all(context.output_fd, std::to_string(rl.rlim_cur) + "\n");
    return 0;
  }
  return 1;
}

int run_times(Context& context) {
  struct rusage self{};
  struct rusage children{};
  getrusage(RUSAGE_SELF, &self);
  getrusage(RUSAGE_CHILDREN, &children);

  std::ostringstream ss;
  auto fmt = [](const struct timeval& tv) {
    long long total_ms = static_cast<long long>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    long long mins = total_ms / 60000;
    long long secs = (total_ms % 60000) / 1000;
    long long ms = total_ms % 1000;
    std::ostringstream out;
    out << mins << "m" << secs << "." << std::setfill('0') << std::setw(3) << ms << "s";
    return out.str();
  };
  ss << fmt(self.ru_utime) << " " << fmt(self.ru_stime) << "\n"
     << fmt(children.ru_utime) << " " << fmt(children.ru_stime) << "\n";
  write_all(context.output_fd, ss.str());
  return 0;
}

int run_getopts(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() < 3) {
    error(context, "getopts: usage: getopts optstring name [arg ...]");
    return 2;
  }
  const std::string optstring = argv[1];
  const std::string var_name = argv[2];

  int optind = 1;
  const std::string optind_str = context.state.value("OPTIND");
  if (!optind_str.empty()) number(optind_str, optind);
  if (optind < 1) optind = 1;

  const std::vector<std::string>& args = argv.size() > 3
                                            ? std::vector<std::string>(argv.begin() + 3, argv.end())
                                            : context.state.positional;

  if (static_cast<std::size_t>(optind) > args.size()) {
    return 1;
  }

  const std::string& current_arg = args[static_cast<std::size_t>(optind - 1)];
  if (current_arg.size() < 2 || current_arg[0] != '-' || current_arg == "--") {
    return 1;
  }

  char opt_char = current_arg[1];
  const std::size_t pos = optstring.find(opt_char);
  if (pos == std::string::npos) {
    context.state.set(var_name, "?");
    context.state.set("OPTARG", std::string(1, opt_char));
    context.state.set("OPTIND", std::to_string(optind + 1));
    return 0;
  }

  context.state.set(var_name, std::string(1, opt_char));
  if (pos + 1 < optstring.size() && optstring[pos + 1] == ':') {
    if (current_arg.size() > 2) {
      context.state.set("OPTARG", current_arg.substr(2));
      context.state.set("OPTIND", std::to_string(optind + 1));
    } else if (static_cast<std::size_t>(optind) < args.size()) {
      context.state.set("OPTARG", args[static_cast<std::size_t>(optind)]);
      context.state.set("OPTIND", std::to_string(optind + 2));
    } else {
      if (optstring.front() == ':') {
        context.state.set(var_name, ":");
        context.state.set("OPTARG", std::string(1, opt_char));
      } else {
        context.state.set(var_name, "?");
        error(context, std::string("getopts: option requires an argument -- ") + opt_char);
      }
      context.state.set("OPTIND", std::to_string(optind + 1));
    }
  } else {
    context.state.set("OPTIND", std::to_string(optind + 1));
  }
  return 0;
}

int run_declare(const std::vector<std::string>& argv, Context& context) {
  bool make_readonly = false;
  bool make_export = false;
  bool is_array = false;
  bool is_assoc = false;

  std::size_t index = 1;
  while (index < argv.size() && argv[index].starts_with("-")) {
    const std::string_view opt = argv[index++];
    for (std::size_t c = 1; c < opt.size(); ++c) {
      if (opt[c] == 'r') make_readonly = true;
      else if (opt[c] == 'x') make_export = true;
      else if (opt[c] == 'a') is_array = true;
      else if (opt[c] == 'A') is_assoc = true;
      else if (opt[c] == 'p') {
        for (const auto& var : context.state.environment()) {
          write_all(context.output_fd, "declare -x " + var + "\n");
        }
        return 0;
      }
    }
  }

  for (; index < argv.size(); ++index) {
    const std::string& arg = argv[index];
    const std::size_t eq = arg.find('=');
    const std::string name = arg.substr(0, eq);
    const std::string val = eq == std::string::npos ? std::string{} : arg.substr(eq + 1);

    if (is_array) {
      if (!context.state.is_array(name)) context.state.set_array(name, {});
    } else if (is_assoc) {
      if (!context.state.is_assoc(name)) context.state.assoc_arrays[name] = {};
    }
    if (eq != std::string::npos || !context.state.is_set(name)) {
      context.state.set(name, val, make_export);
    }
    if (make_export) context.state.mark_exported(name);
    if (make_readonly) context.state.mark_readonly(name);
  }
  return 0;
}

std::string expand_tr_set(std::string_view s) {
  std::string result;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i + 2 < s.size() && s[i + 1] == '-') {
      const char from = s[i];
      const char to = s[i + 2];
      if (from <= to) {
        for (int c = static_cast<unsigned char>(from); c <= static_cast<unsigned char>(to); ++c) {
          result.push_back(static_cast<char>(c));
        }
      } else {
        result.push_back(from);
        result.push_back('-');
        result.push_back(to);
      }
      i += 2;
    } else {
      result.push_back(s[i]);
    }
  }
  return result;
}

int run_tr(const std::vector<std::string>& argv, Context& context) {
  if (argv.size() < 3) return 1;
  const std::string s1 = expand_tr_set(argv[1]);
  const std::string s2 = expand_tr_set(argv[2]);
  unsigned char map[256];
  for (int i = 0; i < 256; ++i) map[i] = static_cast<unsigned char>(i);
  for (std::size_t i = 0; i < s1.size(); ++i) {
    const unsigned char from = static_cast<unsigned char>(s1[i]);
    const unsigned char to = static_cast<unsigned char>(i < s2.size() ? s2[i] : (s2.empty() ? s1[i] : s2.back()));
    map[from] = to;
  }
  char buf[4096];
  while (true) {
    const ssize_t n = ::read(context.input_fd, buf, sizeof(buf));
    if (n <= 0) break;
    for (ssize_t i = 0; i < n; ++i) {
      buf[i] = static_cast<char>(map[static_cast<unsigned char>(buf[i])]);
    }
    write_all(context.output_fd, std::string_view(buf, static_cast<std::size_t>(n)));
  }
  return 0;
}

int run_sleep(const std::vector<std::string>& argv, Context& context) {
  (void)context;
  if (argv.size() < 2) return 1;
  try {
    const double seconds = std::stod(argv[1]);
    if (seconds > 0) {
      struct timespec ts;
      ts.tv_sec = static_cast<time_t>(seconds);
      ts.tv_nsec = static_cast<long>((seconds - static_cast<double>(ts.tv_sec)) * 1000000000.0);
      nanosleep(&ts, nullptr);
    }
    return 0;
  } catch (...) {
    return 1;
  }
}

}  // namespace

std::vector<std::string> names() {
  return {":", ".", "[", "alias", "bg", "break", "cd", "command", "continue", "declare",
          "dirs", "disown", "echo", "eval", "exec", "exit", "export", "false", "fg",
          "getopts", "hash", "help", "history", "jobs", "kill", "local", "mapfile",
          "popd", "printf", "pushd", "pwd", "read", "readarray", "readonly", "return",
          "set", "shift", "sleep", "source", "test", "times", "trap", "true", "tr", "type", "typeset",
          "ulimit", "umask", "unalias", "unset", "wait"};
}

bool is_builtin(std::string_view name) noexcept {
  if (name == "/usr/bin/printf" || name == "/bin/printf" ||
      name == "/usr/bin/true" || name == "/bin/true" ||
      name == "/usr/bin/false" || name == "/bin/false" ||
      name == "/bin/echo" || name == "/usr/bin/echo" ||
      name == "/usr/bin/tr" || name == "/bin/sleep" || name == "/usr/bin/sleep") {
    return true;
  }
  static const auto builtin_names = names();
  return std::find(builtin_names.begin(), builtin_names.end(), name) != builtin_names.end();
}

Result run(const std::vector<std::string>& argv, Context& context) {
  if (argv.empty()) return Result{true, 0, false};
  std::string_view name(argv.front());
  if (name == "/usr/bin/printf" || name == "/bin/printf") name = "printf";
  else if (name == "/usr/bin/true" || name == "/bin/true") name = "true";
  else if (name == "/usr/bin/false" || name == "/bin/false") name = "false";
  else if (name == "/bin/echo" || name == "/usr/bin/echo") name = "echo";
  else if (name == "/usr/bin/tr") name = "tr";
  else if (name == "/bin/sleep" || name == "/usr/bin/sleep") name = "sleep";
  if (!is_builtin(argv.front())) return Result{};
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
  } else if (name == "tr") {
    result.status = run_tr(argv, context);
  } else if (name == "sleep") {
    result.status = run_sleep(argv, context);
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
  } else if (name == "test" || name == "[") {
    result.status = run_test(argv, context);
  } else if (name == "eval") {
    if (argv.size() > 1 && context.eval_script) {
      std::string script;
      for (std::size_t i = 1; i < argv.size(); ++i) {
        if (i > 1) script.push_back(' ');
        script += argv[i];
      }
      result.status = context.eval_script(script);
    }
  } else if (name == "." || name == "source") {
    if (argv.size() < 2) {
      error(context, "source: filename argument required");
      result.status = 2;
    } else if (context.eval_script) {
      std::string filename = argv[1];
      std::ifstream file(filename);
      if (!file.is_open() && filename.find('/') == std::string::npos) {
        const std::string path = context.state.value("PATH");
        std::size_t start = 0;
        while (start <= path.size()) {
          const std::size_t sep = path.find(':', start);
          const std::string dir = path.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
          const std::string cand = (dir.empty() ? "." : dir) + "/" + filename;
          file.open(cand);
          if (file.is_open()) {
            filename = cand;
            break;
          }
          if (sep == std::string::npos) break;
          start = sep + 1;
        }
      }
      if (!file.is_open()) {
        error(context, filename + ": No such file or directory");
        result.status = 1;
      } else {
        std::stringstream ss;
        ss << file.rdbuf();
        result.status = context.eval_script(ss.str());
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
  } else if (name == "read") {
    result.status = run_read(argv, context);
  } else if (name == "mapfile" || name == "readarray") {
    result.status = run_mapfile(argv, context);
  } else if (name == "dirs") {
    result.status = run_dirs(argv, context);
  } else if (name == "pushd") {
    result.status = run_pushd(argv, context);
  } else if (name == "popd") {
    result.status = run_popd(argv, context);
  } else if (name == "alias") {
    result.status = run_alias(argv, context);
  } else if (name == "unalias") {
    result.status = run_unalias(argv, context);
  } else if (name == "trap") {
    result.status = run_trap(argv, context);
  } else if (name == "ulimit") {
    result.status = run_ulimit(argv, context);
  } else if (name == "times") {
    result.status = run_times(context);
  } else if (name == "getopts") {
    result.status = run_getopts(argv, context);
  } else if (name == "declare" || name == "typeset") {
    result.status = run_declare(argv, context);
  } else if (name == "local") {
    for (std::size_t i = 1; i < argv.size(); ++i) {
      const std::string& arg = argv[i];
      const std::size_t eq = arg.find('=');
      const std::string vname = arg.substr(0, eq);
      const std::string val = eq == std::string::npos ? std::string{} : arg.substr(eq + 1);
      context.state.set_local(vname, val);
    }
  } else if (name == "return") {
    context.state.return_requested = true;
    int status = 0;
    if (argv.size() > 1 && number(argv[1], status)) {
      context.state.return_status = status;
    } else {
      context.state.return_status = context.state.last_status;
    }
    result.status = context.state.return_status;
  } else if (name == "break") {
    int levels = 1;
    if (argv.size() > 1) number(argv[1], levels);
    context.state.break_levels = levels > 0 ? levels : 1;
  } else if (name == "continue") {
    int levels = 1;
    if (argv.size() > 1) number(argv[1], levels);
    context.state.continue_levels = levels > 0 ? levels : 1;
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
  } else if (name == "help") {
    write_all(context.output_fd, "Splice builtins: : . [ alias bg break cd command continue declare dirs disown echo eval exec exit export false fg getopts hash help history jobs kill local mapfile popd printf pushd pwd read readarray readonly return set shift source test times trap true type typeset ulimit umask unalias unset wait\n");
  } else if (name == "history") {
    if (argv.size() > 1 && argv[1] == "-c") {
      result.status = 0;
    } else {
      write_all(context.output_fd, "    1  splice\n");
    }
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
  } else if (name == "jobs" || name == "wait" || name == "bg" || name == "fg" || name == "disown") {
    if (context.job_control) {
      result.status = context.job_control(argv);
    } else {
      error(context, std::string(name) + ": job control is provided by the runtime");
      result.status = 1;
    }
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
