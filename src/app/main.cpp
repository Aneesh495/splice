#include "source/source.hpp"
#include "plan/plan.hpp"
#include "syntax/ast.hpp"
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

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options, std::cerr)) return 2;
  if (argc == 1 && isatty(STDIN_FILENO) != 0) {
    std::cout << "Splice " << kVersion << " interactive runtime is being assembled\n";
    return EXIT_SUCCESS;
  }

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
  std::cerr << "splice: parsed input successfully; process runtime is the next implementation boundary\n";
  return 0;
}
