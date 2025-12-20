#pragma once

#include "inspect/trace.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace splice::inspect {

struct ProcessLifetime {
  pid_t pid{-1};
  std::uint64_t start_ns{0};
  std::uint64_t end_ns{0};
  int exit_status{-1};
  std::size_t stage{0};
  std::string command;
  [[nodiscard]] double duration_ms() const noexcept {
    return end_ns > start_ns ? static_cast<double>(end_ns - start_ns) / 1'000'000.0 : 0.0;
  }
};

struct PipelineSummary {
  int job_id{0};
  pid_t pgid{-1};
  std::uint64_t start_ns{0};
  std::uint64_t end_ns{0};
  std::size_t stage_count{0};
  std::vector<ProcessLifetime> processes;
  [[nodiscard]] double duration_ms() const noexcept {
    return end_ns > start_ns ? static_cast<double>(end_ns - start_ns) / 1'000'000.0 : 0.0;
  }
};

struct TraceAnalysis {
  std::vector<TraceEvent> events;
  std::vector<PipelineSummary> pipelines;
  std::uint64_t first_event_ns{0};
  std::uint64_t last_event_ns{0};
  std::size_t total_processes{0};
  [[nodiscard]] double total_duration_ms() const noexcept {
    return last_event_ns > first_event_ns ? static_cast<double>(last_event_ns - first_event_ns) / 1'000'000.0 : 0.0;
  }
};

class ReplayEngine {
 public:
  explicit ReplayEngine(std::string trace_path);

  [[nodiscard]] bool load(std::string& error);
  [[nodiscard]] const TraceAnalysis& analysis() const noexcept { return analysis_; }

  [[nodiscard]] std::string generate_html_report(std::string_view title = "Splice Execution Trace") const;
  [[nodiscard]] std::string dump_summary_json() const;

 private:
  void analyze();
  static std::string escape_html(std::string_view text);

  std::string trace_path_;
  TraceAnalysis analysis_;
};

}  // namespace splice::inspect
