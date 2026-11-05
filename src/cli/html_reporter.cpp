#include "html_reporter.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace fragmentx {
namespace cli {

bool HtmlReporter::generate_report(
    const std::string& output_path,
    const WorkloadModel& model,
    const FragmentationAnalysisResult& frag,
    const OptimizationResult* opt_result)
{
  std::ofstream f(output_path);
  if (!f.is_open()) return false;

  std::ostringstream ss;
  ss << R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>FragmentX — Workload & Heap Optimization Report</title>
  <style>
    :root {
      --bg: #0d1117;
      --card-bg: #161b22;
      --border: #30363d;
      --text: #c9d1d9;
      --heading: #f0f6fc;
      --accent: #58a6ff;
      --success: #3fb950;
      --warning: #d29922;
      --danger: #f85149;
    }
    body {
      margin: 0;
      padding: 24px;
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      line-height: 1.5;
    }
    .container { max-width: 1100px; margin: 0 auto; }
    header { border-bottom: 1px solid var(--border); padding-bottom: 16px; margin-bottom: 24px; }
    h1 { color: var(--heading); margin: 0 0 8px 0; font-size: 24px; }
    .subtitle { color: #8b949e; font-size: 14px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 16px; margin-bottom: 24px; }
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 6px; padding: 16px; }
    .card-title { font-size: 12px; color: #8b949e; text-transform: uppercase; margin-bottom: 6px; }
    .card-val { font-size: 22px; font-weight: 600; color: var(--heading); }
    .card-delta { font-size: 12px; margin-top: 4px; }
    .card-delta.good { color: var(--success); }
    .card-delta.bad { color: var(--warning); }
    table { width: 100%; border-collapse: collapse; margin-top: 12px; }
    th, td { text-align: left; padding: 8px 12px; border-bottom: 1px solid var(--border); font-size: 13px; }
    th { color: #8b949e; background: #0d1117; }
    .badge { display: inline-block; padding: 2px 8px; border-radius: 12px; font-size: 11px; font-weight: 600; }
    .badge-pass { background: rgba(63,185,80,0.15); color: var(--success); }
    .badge-warn { background: rgba(210,153,34,0.15); color: var(--warning); }
    .section-title { color: var(--heading); font-size: 18px; margin: 28px 0 12px 0; }
    .chart-container { background: var(--card-bg); border: 1px solid var(--border); border-radius: 6px; padding: 16px; margin-bottom: 24px; }
    svg { width: 100%; height: 180px; }
  </style>
</head>
<body>
<div class="container">
  <header>
    <h1>FragmentX Optimization Report</h1>
    <div class="subtitle">Adaptive Workload Analysis & Policy Synthesis</div>
  </header>

  <div class="grid">
    <div class="card">
      <div class="card-title">Total Allocations</div>
      <div class="card-val">)" << model.total_allocations << R"(</div>
      <div class="card-delta">)" << model.total_deallocations << R"( frees</div>
    </div>
    <div class="card">
      <div class="card-title">Requested Memory</div>
      <div class="card-val">)" << std::fixed << std::setprecision(1) << (model.total_requested_bytes / 1024.0 / 1024.0) << R"( MB</div>
      <div class="card-delta">)" << model.sites.size() << R"( call sites</div>
    </div>
    <div class="card">
      <div class="card-title">Internal Fragmentation</div>
      <div class="card-val">)" << std::fixed << std::setprecision(2) << (frag.internal.aggregate_waste_fraction * 100.0) << R"(%</div>
      <div class="card-delta bad">)" << std::fixed << std::setprecision(1) << (frag.internal.total_wasted_bytes / 1024.0 / 1024.0) << R"( MB wasted</div>
    </div>
    <div class="card">
      <div class="card-title">APM Optimization</div>
      <div class="card-val">)";

  if (opt_result && opt_result->best_candidate) {
    double imp = opt_result->best_candidate->evaluation.improvement_vs_baseline * 100.0;
    ss << "+" << std::fixed << std::setprecision(1) << imp << "%";
    ss << R"(</div><div class="card-delta good">)" << opt_result->best_candidate->strategy_name << "</div>";
  } else {
    ss << "Baseline</div><div class=\"card-delta\">No policy active</div>";
  }

  ss << R"(
    </div>
  </div>

  <div class="section-title">Allocation Size Distribution</div>
  <div class="chart-container">
    <svg viewBox="0 0 600 150">
  )";

  // Draw SVG Histogram for size classes
  double bar_x = 20;
  for (size_t i = 0; i < 18; ++i) {
    uint64_t count = 0;
    for (const auto& [h, p] : model.sites) {
      count += p.size_dist.histogram[i];
    }
    double h = count > 0 ? std::min(120.0, 10.0 + std::log(count + 1) * 15.0) : 2.0;
    double y = 140.0 - h;
    ss << "<rect x=\"" << bar_x << "\" y=\"" << y << "\" width=\"22\" height=\"" << h
       << "\" fill=\"#58a6ff\" rx=\"2\"></rect>\n";
    bar_x += 32;
  }
  ss << R"(
    </svg>
  </div>

  <div class="section-title">Diagnostics & Identified Patterns</div>
  <div class="card">
  )";

  if (frag.diagnostics.empty()) {
    ss << "<p style='color:var(--success);'>✔ No severe heap fragmentation patterns identified.</p>";
  } else {
    ss << "<table><thead><tr><th>Type</th><th>Severity</th><th>Description</th><th>Recommendation</th></tr></thead><tbody>";
    for (const auto& d : frag.diagnostics) {
      ss << "<tr><td><span class=\"badge badge-warn\">";
      switch (d.type) {
        case FragmentationType::LifetimeMixing: ss << "Lifetime Mixing"; break;
        case FragmentationType::Churn:          ss << "High Churn"; break;
        default:                                ss << "Internal Waste"; break;
      }
      ss << "</span></td><td>" << std::fixed << std::setprecision(2) << d.severity << "</td>"
         << "<td>" << d.description << "</td>"
         << "<td style='color:var(--success);'>" << d.recommendation << "</td></tr>";
    }
    ss << "</tbody></table>";
  }

  ss << R"(
  </div>
</div>
</body>
</html>
  )";

  f << ss.str();
  return f.good();
}

} // namespace cli
} // namespace fragmentx

