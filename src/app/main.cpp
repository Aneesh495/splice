#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view kVersion = "0.1.0-dev";

void print_help(std::ostream& out) {
  out << "Splice, an inspectable Unix shell and process runtime\n"
      << "Usage: splice [OPTIONS] [SCRIPT [ARGUMENT...]]\n\n"
      << "Options:\n"
      << "  -c COMMAND             execute COMMAND\n"
      << "  -n, --no-execute       parse and inspect without executing\n"
      << "  --profile=PROFILE      posix, bash, or splice\n"
      << "  --version              print the version\n"
      << "  -h, --help             print this help\n";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    std::cout << "Splice " << kVersion
              << " (syntax/runtime implementation in progress)\n";
    return EXIT_SUCCESS;
  }
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--version") {
      std::cout << kVersion << '\n';
      return EXIT_SUCCESS;
    }
    if (argument == "-h" || argument == "--help") {
      print_help(std::cout);
      return EXIT_SUCCESS;
    }
    if (argument == "-c") {
      if (index + 1 >= argc) {
        std::cerr << "splice: -c requires a command string\n";
        return 2;
      }
      std::cout << "syntax input accepted (execution runtime pending): "
                << argv[++index] << '\n';
      return EXIT_SUCCESS;
    }
  }
  std::cerr << "splice: invocation accepted; use --help for options\n";
  return 2;
}
