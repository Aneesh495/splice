#include "plan/plan.hpp"

#include <fcntl.h>
#include <sstream>

namespace splice::plan {
namespace {

bool assignment_name(std::string_view value, std::string& name, std::string& assignment) {
  const std::size_t separator = value.find('=');
  if (separator == std::string_view::npos) return false;
  name = std::string(value.substr(0, separator));
  assignment = std::string(value.substr(separator + 1));
  return expand::valid_name(name);
}

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

}  // namespace

const char* descriptor_action_name(DescriptorActionKind kind) noexcept {
  switch (kind) {
    case DescriptorActionKind::Open: return "open";
    case DescriptorActionKind::Append: return "append";
    case DescriptorActionKind::Duplicate: return "duplicate";
    case DescriptorActionKind::Close: return "close";
    case DescriptorActionKind::HereDocument: return "here-document";
  }
  return "unknown";
}

void PlanBuilder::fail(ExecutionPlan& plan, std::string message) {
  if (plan.error.empty()) plan.error = std::move(message);
}

bool PlanBuilder::build_redirection(const syntax::Redirection& redirection,
                                    PlannedCommand& plan) {
  auto target = expander_.word(redirection.target, expand::ExpansionContext::Redirection);
  if (!target.ok()) {
    return false;
  }
  const std::string value = target.fields.empty() ? std::string{} : target.fields.front().value;
  DescriptorAction action;
  action.fd = redirection.fd;
  action.span = redirection.span;
  switch (redirection.kind) {
    case syntax::RedirectionKind::Input:
      action.kind = DescriptorActionKind::Open;
      action.flags = O_RDONLY;
      action.path = value;
      break;
    case syntax::RedirectionKind::Output:
      action.kind = DescriptorActionKind::Open;
      action.flags = O_WRONLY | O_CREAT | O_TRUNC;
      action.path = value;
      break;
    case syntax::RedirectionKind::Append:
      action.kind = DescriptorActionKind::Append;
      action.flags = O_WRONLY | O_CREAT | O_APPEND;
      action.path = value;
      break;
    case syntax::RedirectionKind::DupInput:
    case syntax::RedirectionKind::DupOutput:
      if (value == "-") {
        action.kind = DescriptorActionKind::Close;
      } else {
        try {
          action.kind = DescriptorActionKind::Duplicate;
          action.target_fd = std::stoi(value);
        } catch (...) {
          return false;
        }
      }
      break;
    case syntax::RedirectionKind::HereDocument:
    case syntax::RedirectionKind::HereDocumentStrip:
      action.kind = DescriptorActionKind::HereDocument;
      action.body = value;
      break;
  }
  plan.descriptors.push_back(std::move(action));
  return true;
}

bool PlanBuilder::build_simple(const syntax::SimpleCommand& command, PlannedCommand& plan) {
  plan.span = command.span;
  bool prefix = true;
  for (const auto& word : command.words) {
    auto expanded = expander_.word(word, expand::ExpansionContext::CommandWord);
    if (!expanded.ok()) return false;
    if (expanded.fields.empty()) continue;
    if (prefix && expanded.fields.size() == 1) {
      std::string name;
      std::string value;
      if (assignment_name(expanded.fields.front().value, name, value)) {
        plan.assignments.emplace_back(name, value);
        continue;
      }
    }
    prefix = false;
    for (const auto& field : expanded.fields) plan.argv.push_back(field.value);
  }
  for (const auto& redirection : command.redirections) {
    if (!build_redirection(redirection, plan)) return false;
  }
  plan.environment = state_.environment();
  plan.parent_builtin_eligible = plan.argv.size() > 0;
  return true;
}

bool PlanBuilder::build_command(const syntax::CommandPtr& command, ExecutionPlan& plan) {
  if (command == nullptr) {
    fail(plan, "cannot build a plan for an empty command");
    return false;
  }
  plan.span = command->span;
  plan.negated = command->negated;
  switch (command->kind) {
    case syntax::CommandKind::Simple: {
      PlannedCommand stage;
      if (!build_simple(command->simple, stage)) {
        fail(plan, "word or redirection expansion failed");
        return false;
      }
      plan.stages.push_back(std::move(stage));
      return true;
    }
    case syntax::CommandKind::Pipeline:
      for (const auto& child : command->children) {
        if (!build_command(child, plan)) return false;
      }
      return true;
    case syntax::CommandKind::Background:
      plan.background = true;
      return command->children.empty() ? false : build_command(command->children.front(), plan);
    case syntax::CommandKind::Subshell:
    case syntax::CommandKind::Group:
      if (command->children.empty()) {
        fail(plan, "compound command has no body");
        return false;
      }
      return build_command(command->children.front(), plan);
    default:
      fail(plan, "this command form is not yet executable in the planner");
      return false;
  }
}

ExecutionPlan PlanBuilder::build(const syntax::CommandPtr& command) {
  ExecutionPlan plan;
  build_command(command, plan);
  return plan;
}

std::string PlanBuilder::dump_json(const ExecutionPlan& plan) const {
  std::ostringstream out;
  out << "{\"span\":[" << plan.span.begin << ',' << plan.span.end
      << "],\"background\":" << (plan.background ? "true" : "false")
      << ",\"negated\":" << (plan.negated ? "true" : "false") << ",\"error\":\""
      << json_escape(plan.error) << "\",\"stages\":[";
  for (std::size_t stage_index = 0; stage_index < plan.stages.size(); ++stage_index) {
    if (stage_index != 0) out << ',';
    const auto& stage = plan.stages[stage_index];
    out << "{\"argv\":[";
    for (std::size_t arg_index = 0; arg_index < stage.argv.size(); ++arg_index) {
      if (arg_index != 0) out << ',';
      out << '"' << json_escape(stage.argv[arg_index]) << '"';
    }
    out << "],\"descriptors\":[";
    for (std::size_t action_index = 0; action_index < stage.descriptors.size(); ++action_index) {
      if (action_index != 0) out << ',';
      const auto& action = stage.descriptors[action_index];
      out << "{\"kind\":\"" << descriptor_action_name(action.kind)
          << "\",\"fd\":" << action.fd << ",\"target_fd\":" << action.target_fd
          << ",\"path\":\"" << json_escape(action.path) << "\"}";
    }
    out << "]}";
  }
  out << "]}";
  return out.str();
}

}  // namespace splice::plan
