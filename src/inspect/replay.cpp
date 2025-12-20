#include "inspect/replay.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace splice::inspect {
namespace {

std::string extract_string_field(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":\"";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) return {};
  const std::size_t val_start = start + needle.size();
  const std::size_t val_end = json.find('"', val_start);
  if (val_end == std::string_view::npos) return {};
  return std::string(json.substr(val_start, val_end - val_start));
}

std::uint64_t extract_uint_field(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) return 0;
  const std::size_t val_start = start + needle.size();
  std::size_t val_end = val_start;
  while (val_end < json.size() && std::isdigit(static_cast<unsigned char>(json[val_end]))) {
    ++val_end;
  }
  if (val_start == val_end) return 0;
  try {
    return std::stoull(std::string(json.substr(val_start, val_end - val_start)));
  } catch (...) {
    return 0;
  }
}

std::map<std::string, std::string> extract_fields_map(std::string_view json) {
  std::map<std::string, std::string> fields;
  const std::string needle = "\"fields\":{";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) return fields;
  const std::size_t end = json.find('}', start + needle.size());
  if (end == std::string_view::npos) return fields;

  std::string_view inner = json.substr(start + needle.size(), end - (start + needle.size()));
  std::size_t cursor = 0;
  while (cursor < inner.size()) {
    const std::size_t q1 = inner.find('"', cursor);
    if (q1 == std::string_view::npos) break;
    const std::size_t q2 = inner.find('"', q1 + 1);
    if (q2 == std::string_view::npos) break;
    const std::string k(inner.substr(q1 + 1, q2 - q1 - 1));

    const std::size_t colon = inner.find(':', q2 + 1);
    if (colon == std::string_view::npos) break;
    const std::size_t q3 = inner.find('"', colon + 1);
    if (q3 == std::string_view::npos) break;
    const std::size_t q4 = inner.find('"', q3 + 1);
    if (q4 == std::string_view::npos) break;
    const std::string v(inner.substr(q3 + 1, q4 - q3 - 1));

    fields[k] = v;
    cursor = q4 + 1;
  }
  return fields;
}

}  // namespace

ReplayEngine::ReplayEngine(std::string trace_path) : trace_path_(std::move(trace_path)) {}

std::string ReplayEngine::escape_html(std::string_view text) {
  std::string result;
  for (char ch : text) {
    if (ch == '&') result += "&amp;";
    else if (ch == '<') result += "&lt;";
    else if (ch == '>') result += "&gt;";
    else if (ch == '"') result += "&quot;";
    else if (ch == '\'') result += "&#39;";
    else result.push_back(ch);
  }
  return result;
}

bool ReplayEngine::load(std::string& error) {
  std::ifstream input(trace_path_);
  if (!input.is_open()) {
    error = "cannot open trace file: " + trace_path_;
    return false;
  }

  analysis_.events.clear();
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    TraceEvent event;
    event.sequence = extract_uint_field(line, "seq");
    event.monotonic_ns = extract_uint_field(line, "ts");
    event.kind = extract_string_field(line, "kind");
    event.fields = extract_fields_map(line);
    analysis_.events.push_back(std::move(event));
  }

  if (analysis_.events.empty()) {
    error = "trace file contains no events";
    return false;
  }

  analyze();
  return true;
}

void ReplayEngine::analyze() {
  if (analysis_.events.empty()) return;
  analysis_.first_event_ns = analysis_.events.front().monotonic_ns;
  analysis_.last_event_ns = analysis_.events.back().monotonic_ns;

  std::map<int, PipelineSummary> pipelines_by_id;
  std::map<pid_t, ProcessLifetime> processes_by_pid;

  for (const auto& ev : analysis_.events) {
    if (ev.kind == "pipeline-registered") {
      PipelineSummary summary;
      auto it_job = ev.fields.find("job");
      if (it_job != ev.fields.end()) {
        try { summary.job_id = std::stoi(it_job->second); } catch (...) {}
      }
      auto it_pgid = ev.fields.find("pgid");
      if (it_pgid != ev.fields.end()) {
        try { summary.pgid = static_cast<pid_t>(std::stol(it_pgid->second)); } catch (...) {}
      }
      auto it_stages = ev.fields.find("stages");
      if (it_stages != ev.fields.end()) {
        try { summary.stage_count = std::stoul(it_stages->second); } catch (...) {}
      }
      summary.start_ns = ev.monotonic_ns;
      summary.end_ns = ev.monotonic_ns;
      pipelines_by_id[summary.job_id] = summary;
    } else if (ev.kind == "child-status") {
      pid_t pid = -1;
      auto it_pid = ev.fields.find("pid");
      if (it_pid != ev.fields.end()) {
        try { pid = static_cast<pid_t>(std::stol(it_pid->second)); } catch (...) {}
      }
      int status = 0;
      auto it_status = ev.fields.find("status");
      if (it_status != ev.fields.end()) {
        try { status = std::stoi(it_status->second); } catch (...) {}
      }
      std::size_t stage = 0;
      auto it_stage = ev.fields.find("stage");
      if (it_stage != ev.fields.end()) {
        try { stage = std::stoul(it_stage->second); } catch (...) {}
      }

      ProcessLifetime proc;
      proc.pid = pid;
      proc.stage = stage;
      proc.exit_status = status;
      proc.end_ns = ev.monotonic_ns;
      proc.start_ns = analysis_.first_event_ns;
      for (const auto& [id, pipe] : pipelines_by_id) {
        (void)id;
        if (pipe.start_ns <= ev.monotonic_ns) {
          proc.start_ns = pipe.start_ns;
        }
      }
      processes_by_pid[pid] = proc;
    }
  }

  for (const auto& [pid, proc] : processes_by_pid) {
    (void)pid;
    if (!pipelines_by_id.empty()) {
      pipelines_by_id.rbegin()->second.processes.push_back(proc);
      if (proc.end_ns > pipelines_by_id.rbegin()->second.end_ns) {
        pipelines_by_id.rbegin()->second.end_ns = proc.end_ns;
      }
    }
  }

  analysis_.pipelines.clear();
  for (auto& [id, pipe] : pipelines_by_id) {
    (void)id;
    analysis_.pipelines.push_back(std::move(pipe));
  }
  analysis_.total_processes = processes_by_pid.size();
}

std::string ReplayEngine::dump_summary_json() const {
  std::ostringstream ss;
  ss << "{\"event_count\":" << analysis_.events.size()
     << ",\"pipeline_count\":" << analysis_.pipelines.size()
     << ",\"process_count\":" << analysis_.total_processes
     << ",\"total_duration_ms\":" << std::fixed << std::setprecision(3) << analysis_.total_duration_ms()
     << ",\"pipelines\":[";
  for (std::size_t i = 0; i < analysis_.pipelines.size(); ++i) {
    if (i > 0) ss << ',';
    const auto& pipe = analysis_.pipelines[i];
    ss << "{\"job_id\":" << pipe.job_id
       << ",\"pgid\":" << pipe.pgid
       << ",\"stage_count\":" << pipe.stage_count
       << ",\"duration_ms\":" << std::fixed << std::setprecision(3) << pipe.duration_ms()
       << ",\"processes\":[";
    for (std::size_t j = 0; j < pipe.processes.size(); ++j) {
      if (j > 0) ss << ',';
      const auto& proc = pipe.processes[j];
      ss << "{\"pid\":" << proc.pid
         << ",\"stage\":" << proc.stage
         << ",\"exit_status\":" << proc.exit_status
         << ",\"duration_ms\":" << std::fixed << std::setprecision(3) << proc.duration_ms()
         << "}";
    }
    ss << "]}";
  }
  ss << "]}";
  return ss.str();
}

std::string ReplayEngine::generate_html_report(std::string_view title) const {
  std::ostringstream html;
  html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"UTF-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
       << "<title>" << escape_html(title) << "</title>\n"
       << "<style>\n"
       << "  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; "
       << "         margin: 0; padding: 24px; background: #0f172a; color: #f8fafc; }\n"
       << "  .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #334155; padding-bottom: 16px; margin-bottom: 24px; }\n"
       << "  .header h1 { margin: 0; font-size: 24px; color: #38bdf8; font-weight: 600; }\n"
       << "  .badge { background: #1e293b; border: 1px solid #475569; padding: 4px 10px; border-radius: 6px; font-size: 13px; font-mono; }\n"
       << "  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 28px; }\n"
       << "  .card { background: #1e293b; border: 1px solid #334155; border-radius: 8px; padding: 16px; }\n"
       << "  .card-label { font-size: 12px; text-transform: uppercase; color: #94a3b8; letter-spacing: 0.5px; }\n"
       << "  .card-value { font-size: 28px; font-weight: 700; color: #f1f5f9; margin-top: 6px; }\n"
       << "  .section-title { font-size: 18px; font-weight: 600; margin: 24px 0 12px 0; color: #e2e8f0; }\n"
       << "  .timeline-container { background: #1e293b; border: 1px solid #334155; border-radius: 8px; padding: 16px; margin-bottom: 28px; overflow-x: auto; }\n"
       << "  table { width: 100%; border-collapse: collapse; background: #1e293b; border-radius: 8px; overflow: hidden; border: 1px solid #334155; }\n"
       << "  th { background: #0f172a; color: #94a3b8; text-align: left; padding: 10px 14px; font-size: 13px; border-bottom: 1px solid #334155; }\n"
       << "  td { padding: 10px 14px; font-size: 13px; border-bottom: 1px solid #1e293b; color: #cbd5e1; }\n"
       << "  tr:hover { background: #334155; }\n"
       << "  .pill { display: inline-block; padding: 2px 8px; border-radius: 4px; font-size: 12px; font-weight: 500; }\n"
       << "  .pill-success { background: #064e3b; color: #34d399; }\n"
       << "  .pill-error { background: #7f1d1d; color: #f87171; }\n"
       << "  .pill-info { background: #0c4a6e; color: #38bdf8; }\n"
       << "</style>\n</head>\n<body>\n";

  html << "<div class=\"header\">\n"
       << "  <h1>" << escape_html(title) << "</h1>\n"
       << "  <div class=\"badge\">Splice Trace Engine</div>\n"
       << "</div>\n";

  html << "<div class=\"cards\">\n"
       << "  <div class=\"card\"><div class=\"card-label\">Total Events</div><div class=\"card-value\">"
       << analysis_.events.size() << "</div></div>\n"
       << "  <div class=\"card\"><div class=\"card-label\">Pipelines</div><div class=\"card-value\">"
       << analysis_.pipelines.size() << "</div></div>\n"
       << "  <div class=\"card\"><div class=\"card-label\">Processes Reaped</div><div class=\"card-value\">"
       << analysis_.total_processes << "</div></div>\n"
       << "  <div class=\"card\"><div class=\"card-label\">Elapsed Time</div><div class=\"card-value\">"
       << std::fixed << std::setprecision(2) << analysis_.total_duration_ms() << " ms</div></div>\n"
       << "</div>\n";

  html << "<div class=\"section-title\">Execution Timeline</div>\n"
       << "<div class=\"timeline-container\">\n"
       << "  <svg width=\"100%\" height=\"120\" viewBox=\"0 0 800 120\" xmlns=\"http://www.w3.org/2000/svg\">\n"
       << "    <line x1=\"40\" y1=\"100\" x2=\"760\" y2=\"100\" stroke=\"#475569\" stroke-width=\"2\" />\n";

  const double total_ms = std::max(analysis_.total_duration_ms(), 0.001);
  std::size_t y_offset = 30;
  for (const auto& pipe : analysis_.pipelines) {
    const double rel_start = pipe.start_ns >= analysis_.first_event_ns
                                 ? static_cast<double>(pipe.start_ns - analysis_.first_event_ns) / 1'000'000.0 : 0.0;
    const double rel_dur = std::max(pipe.duration_ms(), 0.05);
    const double x1 = 40.0 + (rel_start / total_ms) * 720.0;
    const double w = std::max((rel_dur / total_ms) * 720.0, 8.0);

    html << "    <rect x=\"" << x1 << "\" y=\"" << y_offset << "\" width=\"" << w
         << "\" height=\"24\" rx=\"4\" fill=\"#38bdf8\" fill-opacity=\"0.8\" />\n"
         << "    <text x=\"" << x1 + 6 << "\" y=\"" << y_offset + 16
         << "\" font-size=\"11\" fill=\"#0f172a\" font-family=\"monospace\" font-weight=\"bold\">job "
         << pipe.job_id << " (" << std::fixed << std::setprecision(1) << pipe.duration_ms() << " ms)</text>\n";
    y_offset += 32;
    if (y_offset > 80) break;
  }

  html << "  </svg>\n</div>\n";

  html << "<div class=\"section-title\">Recorded Events</div>\n"
       << "<table>\n<thead><tr><th>Seq</th><th>Offset (ms)</th><th>Kind</th><th>Attributes</th></tr></thead>\n<tbody>\n";

  for (const auto& ev : analysis_.events) {
    const double offset_ms = ev.monotonic_ns >= analysis_.first_event_ns
                                 ? static_cast<double>(ev.monotonic_ns - analysis_.first_event_ns) / 1'000'000.0 : 0.0;
    html << "<tr>"
         << "<td>" << ev.sequence << "</td>"
         << "<td>+" << std::fixed << std::setprecision(3) << offset_ms << "</td>"
         << "<td><span class=\"pill pill-info\">" << escape_html(ev.kind) << "</span></td>"
         << "<td>";
    bool first_f = true;
    for (const auto& [k, v] : ev.fields) {
      if (!first_f) html << ", ";
      first_f = false;
      html << escape_html(k) << "=\"<strong>" << escape_html(v) << "</strong>\"";
    }
    html << "</td></tr>\n";
  }

  html << "</tbody>\n</table>\n</body>\n</html>\n";
  return html.str();
}

}  // namespace splice::inspect
