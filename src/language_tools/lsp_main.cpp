#include "source/source.hpp"
#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string json_escape(std::string_view value) {
  std::string result;
  for (char character : value) {
    if (character == '"') result += "\\\"";
    else if (character == '\\') result += "\\\\";
    else if (character == '\n') result += "\\n";
    else result.push_back(character);
  }
  return result;
}

std::string field(std::string_view body, std::string_view name) {
  const std::string needle = "\"" + std::string(name) + "\"";
  const std::size_t start = body.find(needle);
  if (start == std::string_view::npos) return {};
  const std::size_t colon = body.find(':', start + needle.size());
  const std::size_t quote = body.find('"', colon + 1);
  const std::size_t end = body.find('"', quote + 1);
  return quote == std::string_view::npos || end == std::string_view::npos ? std::string{} : std::string(body.substr(quote + 1, end - quote - 1));
}

void reply(std::string body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

void diagnostics(const std::string& uri, const std::string& text, int id) {
  splice::source::SourceBuffer source(text, uri);
  splice::syntax::Lexer lexer(source);
  auto lexed = lexer.run();
  splice::syntax::Parser parser(source, lexed.tokens);
  auto parsed = parser.run();
  std::ostringstream result;
  result << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":{\"uri\":\""
          << json_escape(uri) << "\",\"diagnostics\":[";
  bool first = true;
  auto append = [&](const splice::source::Diagnostic& diagnostic) {
    if (!first) result << ',';
    first = false;
    const auto position = source.position(diagnostic.span.begin);
    result << "{\"range\":{\"start\":{\"line\":" << position.line - 1
           << ",\"character\":" << position.column - 1 << "},\"end\":{\"line\":"
           << position.line - 1 << ",\"character\":" << position.column - 1
           << "}},\"severity\":1,\"message\":\"" << json_escape(diagnostic.message) << "\"}";
  };
  for (const auto& diagnostic : lexed.diagnostics.entries) append(diagnostic);
  for (const auto& diagnostic : parsed.diagnostics.entries) append(diagnostic);
  result << "]}}";
  reply(result.str());
}

}  // namespace

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.starts_with("Content-Length:")) {
      std::string header = line;
      std::getline(std::cin, line);
      std::string body(static_cast<std::size_t>(std::stoul(header.substr(15))), '\0');
      std::cin.read(body.data(), static_cast<std::streamsize>(body.size()));
      const std::string method = field(body, "method");
      const std::string id_text = [&]() {
        const std::size_t position = body.find("\"id\"");
        if (position == std::string::npos) return std::string{"0"};
        const std::size_t colon = body.find(':', position);
        const std::size_t end = body.find_first_of(",}", colon + 1);
        return body.substr(colon + 1, end - colon - 1);
      }();
      const int id = std::stoi(id_text);
      if (method == "initialize") {
        reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":{\"capabilities\":{\"hoverProvider\":false,\"completionProvider\":{\"triggerCharacters\":[\"$\"]},\"documentSymbolProvider\":false}}}");
      } else if (method == "shutdown") {
        reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      } else if (method == "splice/check") {
        diagnostics(field(body, "uri"), field(body, "text"), id);
      } else if (method == "exit") {
        return 0;
      } else if (!id_text.empty()) {
        reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      }
    }
  }
  return 0;
}
