#include "source/source.hpp"
#include "syntax/ast.hpp"
#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string json_escape(std::string_view value) {
  std::string result;
  for (char character : value) {
    if (character == '"') result += "\\\"";
    else if (character == '\\') result += "\\\\";
    else if (character == '\n') result += "\\n";
    else if (character == '\r') result += "\\r";
    else if (character == '\t') result += "\\t";
    else result.push_back(character);
  }
  return result;
}

std::string field(std::string_view body, std::string_view name) {
  const std::string needle = "\"" + std::string(name) + "\"";
  const std::size_t start = body.find(needle);
  if (start == std::string_view::npos) return {};
  const std::size_t colon = body.find(':', start + needle.size());
  if (colon == std::string_view::npos) return {};
  const std::size_t quote = body.find('"', colon + 1);
  if (quote == std::string_view::npos) return {};
  const std::size_t end = body.find('"', quote + 1);
  if (end == std::string_view::npos) return {};
  return std::string(body.substr(quote + 1, end - quote - 1));
}

int integer_field(std::string_view body, std::string_view name, int default_val = 0) {
  const std::string needle = "\"" + std::string(name) + "\"";
  const std::size_t start = body.find(needle);
  if (start == std::string_view::npos) return default_val;
  const std::size_t colon = body.find(':', start + needle.size());
  if (colon == std::string_view::npos) return default_val;
  std::size_t digit_start = colon + 1;
  while (digit_start < body.size() && std::isspace(static_cast<unsigned char>(body[digit_start]))) ++digit_start;
  std::size_t digit_end = digit_start;
  while (digit_end < body.size() && (std::isdigit(static_cast<unsigned char>(body[digit_end])) || body[digit_end] == '-')) ++digit_end;
  if (digit_start == digit_end) return default_val;
  try {
    return std::stoi(std::string(body.substr(digit_start, digit_end - digit_start)));
  } catch (...) {
    return default_val;
  }
}

void reply(std::string body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

struct DocumentSymbolInfo {
  std::string name;
  int kind;
  splice::source::Span span;
  std::string detail;
};

void collect_symbols(const splice::syntax::CommandPtr& command,
                     std::vector<DocumentSymbolInfo>& symbols) {
  if (command == nullptr) return;
  if (command->kind == splice::syntax::CommandKind::Function) {
    symbols.push_back(DocumentSymbolInfo{
        command->name,
        12,  // Function symbol kind
        command->span,
        "shell function"
    });
  } else if (command->kind == splice::syntax::CommandKind::Simple) {
    if (!command->simple.words.empty()) {
      const auto& first = command->simple.words.front();
      std::string text;
      for (const auto& part : first.parts) text += part.text;
      const std::size_t eq = text.find('=');
      if (eq != std::string::npos && eq > 0) {
        symbols.push_back(DocumentSymbolInfo{
            text.substr(0, eq),
            13,  // Variable symbol kind
            command->span,
            "variable assignment"
        });
      }
    }
  }
  for (const auto& child : command->children) {
    collect_symbols(child, symbols);
  }
  if (command->if_cmd) {
    for (const auto& clause : command->if_cmd->clauses) {
      collect_symbols(clause.condition, symbols);
      collect_symbols(clause.body, symbols);
    }
    collect_symbols(command->if_cmd->else_body, symbols);
  }
  if (command->for_cmd) {
    symbols.push_back(DocumentSymbolInfo{
        command->for_cmd->name,
        13,
        command->for_cmd->body ? command->for_cmd->body->span : command->span,
        "loop variable"
    });
    collect_symbols(command->for_cmd->body, symbols);
  }
  if (command->while_cmd) {
    collect_symbols(command->while_cmd->condition, symbols);
    collect_symbols(command->while_cmd->body, symbols);
  }
  if (command->case_cmd) {
    for (const auto& item : command->case_cmd->items) {
      collect_symbols(item.body, symbols);
    }
  }
}

std::string builtin_documentation(std::string_view name) {
  if (name == "cd") return "**cd [dir]**\n\nChange the current working directory to *dir* (or `$HOME`).";
  if (name == "pwd") return "**pwd**\n\nPrint the absolute pathname of the current working directory.";
  if (name == "echo") return "**echo [-n] [args...]**\n\nWrite arguments separated by spaces to standard output.";
  if (name == "printf") return "**printf format [args...]**\n\nWrite formatted output according to format specification.";
  if (name == "export") return "**export [name[=val]...]**\n\nMark names for automatic export to the environment of subsequent commands.";
  if (name == "unset") return "**unset [-v] [-f] [name...]**\n\nUnset values and attributes of variables and functions.";
  if (name == "readonly") return "**readonly [name...]**\n\nMark variables as unalterable and cannot be unset.";
  if (name == "set") return "**set [-e] [-u] [-o pipefail] [-- args...]**\n\nSet or unset values of shell options and positional parameters.";
  if (name == "shift") return "**shift [n]**\n\nShift positional parameters to the left by *n*.";
  if (name == "test" || name == "[") return "**test expr** / **[ expr ]**\n\nEvaluate conditional expression.";
  if (name == "read") return "**read [-r] [-p prompt] [-a array] [name...]**\n\nRead a line from standard input and split into fields.";
  if (name == "mapfile" || name == "readarray") return "**mapfile [-t] [-n count] [array]**\n\nRead lines from standard input into an indexed array.";
  if (name == "dirs") return "**dirs**\n\nDisplay the list of currently remembered directories.";
  if (name == "pushd") return "**pushd dir**\n\nSave the current directory on directory stack and change directory to *dir*.";
  if (name == "popd") return "**popd**\n\nRemove entries from directory stack and navigate to top entry.";
  if (name == "alias") return "**alias [name[=val]...]**\n\nDefine or display aliases.";
  if (name == "unalias") return "**unalias [-a] name...**\n\nRemove alias definitions.";
  if (name == "trap") return "**trap [action] [signal...]**\n\nTrap and respond to signals and shell events.";
  if (name == "ulimit") return "**ulimit [-a] [-n] [-s] [limit]**\n\nControl user process resource limits.";
  if (name == "times") return "**times**\n\nPrint accumulated user and system process times.";
  if (name == "getopts") return "**getopts optstring name [arg...]**\n\nParse positional parameters as options.";
  if (name == "jobs") return "**jobs**\n\nList active asynchronous jobs and process group IDs.";
  if (name == "wait") return "**wait [job...]**\n\nWait for background jobs to terminate and report exit status.";
  if (name == "bg") return "**bg [job...]**\n\nResume stopped jobs in background.";
  if (name == "fg") return "**fg [job...]**\n\nMove background jobs to foreground controlling terminal.";
  if (name == "disown") return "**disown [job...]**\n\nRemove jobs from active job table.";
  if (name == "exit") return "**exit [status]**\n\nExit shell with specified status code.";
  return {};
}

class Server {
 public:
  void handle(std::string_view body) {
    const std::string method = field(body, "method");
    const std::string id_text = extract_id(body);
    const int id = id_text.empty() ? 0 : std::stoi(id_text);

    if (method == "initialize") {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text +
            ",\"result\":{\"capabilities\":{"
            "\"textDocumentSync\":1,"
            "\"hoverProvider\":true,"
            "\"completionProvider\":{\"triggerCharacters\":[\"$\",\"-\",\"/\"]},"
            "\"documentSymbolProvider\":true,"
            "\"definitionProvider\":true}}}");
    } else if (method == "shutdown") {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
    } else if (method == "exit") {
      std::exit(0);
    } else if (method == "textDocument/didOpen") {
      const std::string uri = field(body, "uri");
      const std::string text = field(body, "text");
      if (!uri.empty()) documents_[uri] = text;
      publish_diagnostics(uri, text);
    } else if (method == "textDocument/didChange") {
      const std::string uri = field(body, "uri");
      const std::string text = field(body, "text");
      if (!uri.empty() && !text.empty()) {
        documents_[uri] = text;
        publish_diagnostics(uri, text);
      }
    } else if (method == "splice/check") {
      diagnostics_request(field(body, "uri"), field(body, "text"), id);
    } else if (method == "textDocument/documentSymbol") {
      handle_document_symbols(body, id_text);
    } else if (method == "textDocument/hover") {
      handle_hover(body, id_text);
    } else if (method == "textDocument/completion") {
      handle_completion(body, id_text);
    } else if (method == "textDocument/definition") {
      handle_definition(body, id_text);
    } else if (!id_text.empty()) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
    }
  }

 private:
  std::string extract_id(std::string_view body) {
    const std::size_t position = body.find("\"id\"");
    if (position == std::string_view::npos) return {};
    const std::size_t colon = body.find(':', position);
    if (colon == std::string_view::npos) return {};
    const std::size_t end = body.find_first_of(",}", colon + 1);
    if (end == std::string_view::npos) return {};
    std::size_t s = colon + 1;
    while (s < end && std::isspace(static_cast<unsigned char>(body[s]))) ++s;
    std::size_t e = end;
    while (e > s && std::isspace(static_cast<unsigned char>(body[e - 1]))) --e;
    return std::string(body.substr(s, e - s));
  }

  void diagnostics_request(const std::string& uri, const std::string& text, int id) {
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

  void publish_diagnostics(const std::string& uri, const std::string& text) {
    splice::source::SourceBuffer source(text, uri);
    splice::syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    splice::syntax::Parser parser(source, lexed.tokens);
    auto parsed = parser.run();

    std::ostringstream result;
    result << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\""
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

  void handle_document_symbols(std::string_view body, const std::string& id_text) {
    const std::string uri = field(body, "uri");
    const auto it = documents_.find(uri);
    if (it == documents_.end()) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":[]}");
      return;
    }
    splice::source::SourceBuffer source(it->second, uri);
    splice::syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    splice::syntax::Parser parser(source, lexed.tokens);
    auto parsed = parser.run();

    std::vector<DocumentSymbolInfo> symbols;
    for (const auto& cmd : parsed.program.commands) {
      collect_symbols(cmd, symbols);
    }

    std::ostringstream result;
    result << "{\"jsonrpc\":\"2.0\",\"id\":" << id_text << ",\"result\":[";
    for (std::size_t i = 0; i < symbols.size(); ++i) {
      if (i > 0) result << ',';
      const auto& sym = symbols[i];
      const auto pos_start = source.position(sym.span.begin);
      const auto pos_end = source.position(sym.span.end);
      result << "{\"name\":\"" << json_escape(sym.name) << "\",\"kind\":" << sym.kind
             << ",\"detail\":\"" << json_escape(sym.detail) << "\",\"range\":{"
             << "\"start\":{\"line\":" << pos_start.line - 1 << ",\"character\":" << pos_start.column - 1 << "},"
             << "\"end\":{\"line\":" << pos_end.line - 1 << ",\"character\":" << pos_end.column - 1 << "}},"
             << "\"selectionRange\":{"
             << "\"start\":{\"line\":" << pos_start.line - 1 << ",\"character\":" << pos_start.column - 1 << "},"
             << "\"end\":{\"line\":" << pos_end.line - 1 << ",\"character\":" << pos_end.column - 1 << "}}}";
    }
    result << "]}";
    reply(result.str());
  }

  void handle_hover(std::string_view body, const std::string& id_text) {
    const std::string uri = field(body, "uri");
    const int line = integer_field(body, "line", -1);
    const int character = integer_field(body, "character", -1);

    const auto it = documents_.find(uri);
    if (it == documents_.end() || line < 0 || character < 0) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      return;
    }

    const std::string& doc = it->second;
    std::size_t current_line = 0;
    std::size_t line_start = 0;
    for (std::size_t i = 0; i < doc.size(); ++i) {
      if (current_line == static_cast<std::size_t>(line)) break;
      if (doc[i] == '\n') {
        ++current_line;
        line_start = i + 1;
      }
    }
    const std::size_t offset = line_start + static_cast<std::size_t>(character);
    if (offset >= doc.size()) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      return;
    }

    std::size_t w_start = offset;
    while (w_start > line_start && !std::isspace(static_cast<unsigned char>(doc[w_start - 1])) && doc[w_start - 1] != ';' && doc[w_start - 1] != '|') {
      --w_start;
    }
    std::size_t w_end = offset;
    while (w_end < doc.size() && doc[w_end] != '\n' && !std::isspace(static_cast<unsigned char>(doc[w_end])) && doc[w_end] != ';' && doc[w_end] != '|') {
      ++w_end;
    }
    const std::string word = doc.substr(w_start, w_end - w_start);

    std::string doc_content = builtin_documentation(word);
    if (doc_content.empty() && word.starts_with("$")) {
      doc_content = "Parameter reference: `" + word + "`";
    }
    if (doc_content.empty()) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      return;
    }

    std::ostringstream result;
    result << "{\"jsonrpc\":\"2.0\",\"id\":" << id_text << ",\"result\":{"
           << "\"contents\":{\"kind\":\"markdown\",\"value\":\"" << json_escape(doc_content) << "\"}}}";
    reply(result.str());
  }

  void handle_completion(std::string_view body, const std::string& id_text) {
    (void)body;
    const std::string_view keywords[] = {
        "if", "then", "else", "elif", "fi", "for", "in", "do", "done",
        "while", "until", "case", "esac", "function", "select", "time"};
    const std::string_view builtins_list[] = {
        ":", ".", "[", "alias", "bg", "break", "cd", "command", "continue", "declare",
        "dirs", "disown", "echo", "eval", "exec", "exit", "export", "false", "fg",
        "getopts", "hash", "help", "history", "jobs", "kill", "local", "mapfile",
        "popd", "printf", "pushd", "pwd", "read", "readarray", "readonly", "return",
        "set", "shift", "source", "test", "times", "trap", "true", "type", "typeset",
        "ulimit", "umask", "unalias", "unset", "wait"};

    std::ostringstream result;
    result << "{\"jsonrpc\":\"2.0\",\"id\":" << id_text << ",\"result\":{\"isIncomplete\":false,\"items\":[";
    bool first = true;
    for (const auto& kw : keywords) {
      if (!first) result << ',';
      first = false;
      result << "{\"label\":\"" << kw << "\",\"kind\":14,\"detail\":\"shell keyword\"}";
    }
    for (const auto& b : builtins_list) {
      if (!first) result << ',';
      first = false;
      result << "{\"label\":\"" << b << "\",\"kind\":3,\"detail\":\"shell builtin\"}";
    }
    result << "]}}";
    reply(result.str());
  }

  void handle_definition(std::string_view body, const std::string& id_text) {
    const std::string uri = field(body, "uri");
    const int line = integer_field(body, "line", -1);
    const int character = integer_field(body, "character", -1);

    const auto it = documents_.find(uri);
    if (it == documents_.end() || line < 0 || character < 0) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      return;
    }

    const std::string& doc = it->second;
    std::size_t current_line = 0;
    std::size_t line_start = 0;
    for (std::size_t i = 0; i < doc.size(); ++i) {
      if (current_line == static_cast<std::size_t>(line)) break;
      if (doc[i] == '\n') {
        ++current_line;
        line_start = i + 1;
      }
    }
    const std::size_t offset = line_start + static_cast<std::size_t>(character);
    if (offset >= doc.size()) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      return;
    }

    std::size_t w_start = offset;
    while (w_start > line_start && (std::isalnum(static_cast<unsigned char>(doc[w_start - 1])) || doc[w_start - 1] == '_')) {
      --w_start;
    }
    std::size_t w_end = offset;
    while (w_end < doc.size() && (std::isalnum(static_cast<unsigned char>(doc[w_end])) || doc[w_end] == '_')) {
      ++w_end;
    }
    const std::string target = doc.substr(w_start, w_end - w_start);
    if (target.empty()) {
      reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
      return;
    }

    splice::source::SourceBuffer source(doc, uri);
    splice::syntax::Lexer lexer(source);
    auto lexed = lexer.run();
    splice::syntax::Parser parser(source, lexed.tokens);
    auto parsed = parser.run();

    std::vector<DocumentSymbolInfo> symbols;
    for (const auto& cmd : parsed.program.commands) {
      collect_symbols(cmd, symbols);
    }

    for (const auto& sym : symbols) {
      if (sym.name == target && sym.kind == 12) {  // Function
        const auto p_start = source.position(sym.span.begin);
        const auto p_end = source.position(sym.span.end);
        std::ostringstream result;
        result << "{\"jsonrpc\":\"2.0\",\"id\":" << id_text << ",\"result\":{"
               << "\"uri\":\"" << json_escape(uri) << "\",\"range\":{"
               << "\"start\":{\"line\":" << p_start.line - 1 << ",\"character\":" << p_start.column - 1 << "},"
               << "\"end\":{\"line\":" << p_end.line - 1 << ",\"character\":" << p_end.column - 1 << "}}}}";
        reply(result.str());
        return;
      }
    }

    reply("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":null}");
  }

  std::map<std::string, std::string> documents_;
};

}  // namespace

int main() {
  Server server;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.starts_with("Content-Length:")) {
      std::string header = line;
      std::getline(std::cin, line);
      const std::size_t length = static_cast<std::size_t>(std::stoul(header.substr(15)));
      std::string body(length, '\0');
      std::cin.read(body.data(), static_cast<std::streamsize>(length));
      server.handle(body);
    }
  }
  return 0;
}
