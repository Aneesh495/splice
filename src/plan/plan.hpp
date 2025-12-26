#pragma once

#include "expand/expander.hpp"
#include "syntax/ast.hpp"

#include <string>
#include <utility>
#include <vector>

namespace splice::plan {

enum class DescriptorActionKind { Open, Append, Duplicate, Close, HereDocument };

struct DescriptorAction {
  DescriptorActionKind kind{DescriptorActionKind::Open};
  int fd{-1};
  int target_fd{-1};
  int flags{0};
  std::string path;
  std::string body;
  source::Span span{};
};

struct PlannedCommand {
  source::Span span{};
  std::vector<std::string> argv;
  std::vector<std::string> environment;
  std::vector<std::pair<std::string, std::string>> assignments;
  std::vector<DescriptorAction> descriptors;
  bool merge_stderr{false};
  bool parent_builtin_eligible{false};
  syntax::CommandPtr compound_node;
};

struct ExecutionPlan {
  source::Span span{};
  std::vector<PlannedCommand> stages;
  bool background{false};
  bool negated{false};
  std::string error;

  [[nodiscard]] bool valid() const noexcept { return error.empty() && !stages.empty(); }
};

class PlanBuilder {
 public:
  explicit PlanBuilder(expand::ShellState& state,
                       expand::CommandSubstitution substitution = {})
      : state_(state), expander_(state, std::move(substitution)) {}

  [[nodiscard]] ExecutionPlan build(const syntax::CommandPtr& command);
  [[nodiscard]] std::string dump_json(const ExecutionPlan& plan) const;

 private:
  bool build_command(const syntax::CommandPtr& command, ExecutionPlan& plan);
  bool build_simple(const syntax::SimpleCommand& command, PlannedCommand& plan);
  bool build_redirection(const syntax::Redirection& redirection, PlannedCommand& plan);
  void fail(ExecutionPlan& plan, std::string message);

  expand::ShellState& state_;
  expand::Expander expander_;
};

[[nodiscard]] const char* descriptor_action_name(DescriptorActionKind kind) noexcept;

}  // namespace splice::plan
