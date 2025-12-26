#include "source/source.hpp"
#include "plan/plan.hpp"
#include "inspect/trace.hpp"
#include "inspect/replay.hpp"
#include "interactive/editor.hpp"
#include "history/history.hpp"
#include "runtime/runtime.hpp"
#include "task/runner.hpp"
#include "syntax/ast.hpp"
#include "syntax/formatter.hpp"
#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace splice;

namespace {
constexpr std::string_view kVersion = "0.2.0-dev";

struct Options {
  bool no_execute{false};
  bool dump_tokens{false};
  bool dump_ast{false};
  bool dump_plan{false};
  std::string trace_path;
  std::string replay_path;
  std::string html_path;
  std::string command;
  std::string script;
  std::string profile{"posix"};
};

void print_help(std::ostream& out) {
  out << "Splice, an inspectable Unix shell and process runtime\n"
      << "Usage: splice [OPTIONS] [SCRIPT [ARGUMENT...]]\n\n"
      << "Options:\n"
      << "  -c COMMAND             execute COMMAND\n"
      << "  -n, --no-execute       parse without executing\n"
      << "  --dump-tokens          print quote-preserving tokens as JSON\n"
      << "  --dump-ast             print the typed AST as JSON\n"
      << "  --dump-plan            print the expanded descriptor plan as JSON\n"
      << "  --trace FILE           write versioned runtime events as JSONL\n"
      << "  run --manifest FILE    execute a bounded task manifest\n"
      << "  format [--check] FILE  format a shell script canonically\n"
      << "  lint [--json] FILE     statically lint shell scripts for hazards\n"
      << "  --profile=PROFILE      posix, bash, or splice\n"
      << "  --version              print the version\n"
      << "  -h, --help             print this help\n";
}

bool parse_options(int argc, char** argv, Options& options, std::ostream& errors) {
  bool positional_only = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (positional_only) {
      if (options.script.empty()) options.script = std::string(argument);
      continue;
    }
    if (argument == "--") {
      positional_only = true;
    } else if (argument == "-n" || argument == "--no-execute") {
      options.no_execute = true;
    } else if (argument == "--dump-tokens" || argument == "--dump-tokens=json") {
      options.no_execute = true;
      options.dump_tokens = true;
    } else if (argument == "--dump-ast" || argument == "--dump-ast=json") {
      options.no_execute = true;
      options.dump_ast = true;
    } else if (argument == "--dump-plan" || argument == "--dump-plan=json") {
      options.no_execute = true;
      options.dump_plan = true;
    } else if (argument == "--trace") {
      if (index + 1 >= argc) {
        errors << "splice: --trace requires a file\n";
        return false;
      }
      options.trace_path = argv[++index];
    } else if (argument == "--replay") {
      if (index + 1 >= argc) {
        errors << "splice: --replay requires a trace file\n";
        return false;
      }
      options.replay_path = argv[++index];
    } else if (argument == "--html") {
      if (index + 1 >= argc) {
        errors << "splice: --html requires an output path\n";
        return false;
      }
      options.html_path = argv[++index];
    } else if (argument == "--version") {
      std::cout << kVersion << '\n';
      std::exit(EXIT_SUCCESS);
    } else if (argument == "-h" || argument == "--help") {
      print_help(std::cout);
      std::exit(EXIT_SUCCESS);
    } else if (argument == "-c") {
      if (index + 1 >= argc) {
        errors << "splice: -c requires a command string\n";
        return false;
      }
      options.command = argv[++index];
    } else if (argument.starts_with("--profile=")) {
      options.profile = std::string(argument.substr(10));
      if (options.profile != "posix" && options.profile != "bash" && options.profile != "splice") {
        errors << "splice: unsupported profile: " << options.profile << '\n';
        return false;
      }
    } else if (!argument.empty() && argument.front() == '-') {
      errors << "splice: unknown option: " << argument << '\n';
      return false;
    } else if (options.script.empty()) {
      options.script = std::string(argument);
    }
  }
  return true;
}

bool read_source(const Options& options, std::string& text, std::string& name,
                 std::ostream& errors) {
  if (!options.command.empty()) {
    text = options.command;
    name = "<command>";
    return true;
  }
  if (!options.script.empty()) {
    std::ifstream input(options.script);
    if (!input) {
      errors << "splice: cannot open " << options.script << ": " << std::strerror(errno) << '\n';
      return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
    name = options.script;
    return true;
  }
  if (isatty(STDIN_FILENO) != 0) return false;
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  text = buffer.str();
  name = "<stdin>";
  return true;
}

void print_diagnostics(const source::SourceBuffer& source,
                       const source::Diagnostics& diagnostics, std::ostream& errors) {
  for (const auto& diagnostic : diagnostics.entries) {
    const auto position = source.position(diagnostic.span.begin);
    errors << source.name() << ':' << position.line << ':' << position.column
           << ": " << diagnostic.label() << ": " << diagnostic.message << '\n';
    if (!diagnostic.repair.empty()) errors << "  help: " << diagnostic.repair << '\n';
  }
}

int run_task_cli(int argc, char** argv) {
  std::size_t max_parallel = 1;
  std::string manifest;
  for (int index = 2; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--max-parallel" && index + 1 < argc) {
      try { max_parallel = std::stoul(argv[++index]); }
      catch (...) { std::cerr << "splice run: invalid parallelism\n"; return 2; }
    } else if (argument == "--manifest" && index + 1 < argc) {
      manifest = argv[++index];
    } else {
      std::cerr << "splice run: expected --max-parallel N --manifest FILE\n";
      return 2;
    }
  }
  if (manifest.empty()) {
    std::cerr << "splice run: manifest is required\n";
    return 2;
  }
  task::Runner runner(max_parallel);
  std::string error;
  if (!runner.load(manifest, error)) {
    std::cerr << "splice run: " << error << '\n';
    return 2;
  }
  const auto outcomes = runner.run(error);
  if (!error.empty()) {
    std::cerr << "splice run: " << error << '\n';
    return 125;
  }
  std::cout << runner.dump_json(outcomes) << '\n';
  for (const auto& outcome : outcomes) if (outcome.status != 0) return 1;
  return 0;
}

int run_interactive(const Options& options) {
  expand::ShellState state;
  expand::Profile profile;
  if (!expand::parse_profile(options.profile, profile)) return 2;
  state.set_profile(profile);
  state.set("0", "splice");
  const char* configured_history = std::getenv("SPLICE_HISTORY");
  const char* home = std::getenv("HOME");
  const std::string history_path = configured_history != nullptr ? configured_history :
      (home != nullptr ? std::string(home) + "/.splice/history" : ".splice-history");
  history::Store history_store(history_path);
  (void)history_store.open();
  inspect::Trace trace(options.trace_path);
  if (!options.trace_path.empty() && !trace.open()) {
    std::cerr << "splice: cannot open trace " << options.trace_path << '\n';
    return 2;
  }
  runtime::Runtime runtime(state, options.trace_path.empty() ? nullptr : &trace);
  interactive::LineEditor editor(STDIN_FILENO, STDOUT_FILENO, history_store, &state);
  while (true) {
    std::string line;
    const std::string prompt = "splice[" + std::to_string(state.last_status) + "]$ ";
    if (!editor.read_line(prompt, line)) break;
    if (line.empty()) continue;
    std::string program_text = line;
    syntax::ParseResult parsed;
    source::SourceBuffer source;
    syntax::LexResult lexed;
    while (true) {
      source = source::SourceBuffer(program_text, "<interactive>");
      syntax::Lexer lexer(source);
      lexed = lexer.run();
      syntax::Parser parser(source, lexed.tokens);
      parsed = parser.run();
      if (!parsed.incomplete && !lexed.incomplete) break;
      std::string continuation;
      if (!editor.read_line("> ", continuation)) return 0;
      program_text += '\n';
      program_text += continuation;
    }
    editor.prepare_execute();
    print_diagnostics(source, lexed.diagnostics, std::cerr);
    print_diagnostics(source, parsed.diagnostics, std::cerr);
    int status = 2;
    if (!lexed.diagnostics.has_error() && !parsed.diagnostics.has_error()) status = runtime.execute(parsed.program);
    (void)history_store.append(history::Entry{program_text, 0, status, state.current_directory});
    if (runtime.exit_requested()) break;
    editor.resume_input();
  }
  return state.last_status;
}

int run_format_cli(int argc, char** argv) {
  bool check = false;
  std::size_t indent_width = 2;
  std::vector<std::string> files;
  for (int index = 2; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--check") {
      check = true;
    } else if (arg == "--indent" && index + 1 < argc) {
      try { indent_width = std::stoul(argv[++index]); }
      catch (...) { std::cerr << "splice format: invalid indent\n"; return 2; }
    } else if (!arg.starts_with("-")) {
      files.emplace_back(arg);
    }
  }

  syntax::FormatOptions options;
  options.indent_width = indent_width;
  syntax::Formatter formatter(options);

  if (files.empty()) {
    std::ostringstream buf;
    buf << std::cin.rdbuf();
    const std::string text = buf.str();
    source::SourceBuffer source(text, "<stdin>");
    syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    syntax::Parser parser(source, lexed.tokens);
    auto parsed = parser.run();
    if (lexed.diagnostics.has_error() || parsed.diagnostics.has_error()) {
      std::cerr << "splice format: syntax errors in input\n";
      return 1;
    }
    const std::string formatted = formatter.format(parsed.program);
    if (check) {
      return (formatted == text) ? 0 : 1;
    }
    std::cout << formatted;
    return 0;
  }

  int exit_code = 0;
  for (const auto& file : files) {
    std::ifstream in(file);
    if (!in) {
      std::cerr << "splice format: cannot open " << file << ": " << std::strerror(errno) << '\n';
      exit_code = 2;
      continue;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();
    const std::string text = buf.str();
    source::SourceBuffer source(text, file);
    syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    syntax::Parser parser(source, lexed.tokens);
    auto parsed = parser.run();
    if (lexed.diagnostics.has_error() || parsed.diagnostics.has_error()) {
      std::cerr << "splice format: syntax error in " << file << '\n';
      exit_code = 1;
      continue;
    }
    const std::string formatted = formatter.format(parsed.program);
    if (check) {
      if (formatted != text) {
        std::cerr << "splice format: " << file << " needs formatting\n";
        exit_code = 1;
      }
    } else {
      std::ofstream out(file);
      if (!out) {
        std::cerr << "splice format: cannot write " << file << ": " << std::strerror(errno) << '\n';
        exit_code = 2;
        continue;
      }
      out << formatted;
    }
  }
  return exit_code;
}

int run_lint_cli(int argc, char** argv) {
  bool json_output = false;
  std::vector<std::string> files;
  for (int index = 2; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--json") {
      json_output = true;
    } else if (!arg.starts_with("-")) {
      files.emplace_back(arg);
    }
  }

  if (files.empty()) {
    std::cerr << "splice lint: no files specified\n";
    return 2;
  }

  syntax::Linter linter;
  std::vector<syntax::Diagnostic> all_diags;
  int exit_code = 0;

  for (const auto& file : files) {
    std::ifstream in(file);
    if (!in) {
      std::cerr << "splice lint: cannot open " << file << ": " << std::strerror(errno) << '\n';
      exit_code = 2;
      continue;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();
    source::SourceBuffer source(text, file);
    syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    syntax::Parser parser(source, lexed.tokens);
    auto parsed = parser.run();
    if (lexed.diagnostics.has_error() || parsed.diagnostics.has_error()) {
      std::cerr << "splice lint: syntax error in " << file << '\n';
      exit_code = 1;
      continue;
    }
    auto diags = linter.lint(parsed.program, &source);
    if (!diags.empty()) exit_code = 1;
    for (auto& d : diags) all_diags.push_back(std::move(d));
  }

  if (json_output) {
    std::cout << "{\"diagnostics\":[";
    for (std::size_t i = 0; i < all_diags.size(); ++i) {
      if (i > 0) std::cout << ",";
      std::cout << all_diags[i].to_json();
    }
    std::cout << "]}\n";
  } else {
    for (const auto& d : all_diags) {
      std::cerr << d.format() << '\n';
    }
  }
  return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "run") return run_task_cli(argc, argv);
  if (argc > 1 && std::string_view(argv[1]) == "format") return run_format_cli(argc, argv);
  if (argc > 1 && std::string_view(argv[1]) == "lint") return run_lint_cli(argc, argv);
  Options options;
  if (!parse_options(argc, argv, options, std::cerr)) return 2;

  if (!options.replay_path.empty()) {
    inspect::ReplayEngine engine(options.replay_path);
    std::string error;
    if (!engine.load(error)) {
      std::cerr << "splice: " << error << '\n';
      return 2;
    }
    if (!options.html_path.empty()) {
      std::ofstream html_out(options.html_path);
      if (!html_out.is_open()) {
        std::cerr << "splice: cannot write HTML to " << options.html_path << '\n';
        return 2;
      }
      html_out << engine.generate_html_report();
    }
    std::cout << engine.dump_summary_json() << '\n';
    return 0;
  }

  if (argc == 1 && isatty(STDIN_FILENO) != 0) return run_interactive(options);

  std::string text;
  std::string name;
  if (!read_source(options, text, name, std::cerr)) {
    if (options.command.empty() && options.script.empty()) {
      std::cerr << "splice: no script or command supplied\n";
    }
    return 2;
  }
  source::SourceBuffer source(std::move(text), std::move(name));
  syntax::Lexer lexer(source);
  auto lexed = lexer.run();
  if (options.dump_tokens) std::cout << syntax::dump_tokens_json(lexed.tokens) << '\n';
  syntax::Parser parser(source, lexed.tokens);
  auto parsed = parser.run();
  print_diagnostics(source, lexed.diagnostics, std::cerr);
  print_diagnostics(source, parsed.diagnostics, std::cerr);
  if (options.dump_ast) std::cout << syntax::dump_ast_json(parsed.program) << '\n';
  if (lexed.diagnostics.has_error() || parsed.diagnostics.has_error()) return 2;
  if (parsed.incomplete || lexed.incomplete) {
    std::cerr << source.name() << ": incomplete input\n";
    return 2;
  }
  if (options.dump_plan) {
    expand::ShellState state;
    expand::Profile profile;
    if (expand::parse_profile(options.profile, profile)) state.set_profile(profile);
    plan::PlanBuilder builder(state);
    for (const auto& command : parsed.program.commands) {
      const auto execution_plan = builder.build(command);
      std::cout << builder.dump_json(execution_plan) << '\n';
      if (!execution_plan.valid()) {
        std::cerr << source.name() << ": plan: " << execution_plan.error << '\n';
        return 2;
      }
    }
    return EXIT_SUCCESS;
  }
  if (options.no_execute) return EXIT_SUCCESS;
  expand::ShellState state;
  expand::Profile profile;
  if (!expand::parse_profile(options.profile, profile)) return 2;
  state.set_profile(profile);
  state.set("0", options.script.empty() ? "splice" : options.script);
  inspect::Trace trace(options.trace_path);
  if (!options.trace_path.empty() && !trace.open()) {
    std::cerr << "splice: cannot open trace " << options.trace_path << '\n';
    return 2;
  }
  runtime::Runtime runtime(state, options.trace_path.empty() ? nullptr : &trace);
  return runtime.execute(parsed.program);
}
