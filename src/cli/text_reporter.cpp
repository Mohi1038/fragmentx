#include "text_reporter.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fragmentx {
namespace cli {

// ANSI color codes
static const char* RESET  = "\033[0m";
static const char* BOLD   = "\033[1m";
static const char* GREEN  = "\033[32m";
static const char* YELLOW = "\033[33m";
static const char* CYAN   = "\033[36m";
static const char* RED    = "\033[31m";

void TextReporter::print_banner(std::ostream& os) {
  os << CYAN << BOLD << R"(
  ███████╗██████╗  █████╗  ██████╗ ███╗   ███╗███████╗███╗   ██╗████████╗██╗  ██╗
  ██╔════╝██╔══██╗██╔══██╗██╔════╝ ████╗ ████║██╔════╝████╗  ██║╚══██╔══╝╚██╗██╔╝
  █████╗  ██████╔╝███████║██║  ███╗██╔████╔██║█████╗  ██╔██╗ ██║   ██║    ╚███╔╝ 
  ██╔══╝  ██╔══██╗██╔══██║██║   ██║██║╚██╔╝██║██╔══╝  ██║╚██╗██║   ██║    ██╔██╗ 
  ██║     ██║  ██║██║  ██║╚██████╔╝██║ ╚═╝ ██║███████╗██║ ╚████║   ██║   ██╔╝ ██╗
  ╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝╚══════╝╚═╝  ╚═══╝   ╚═╝   ╚═╝  ╚═╝
)" << RESET << "\n";
  os << "       " << BOLD << "Adaptive Memory Allocation & Heap Optimization for C/C++" << RESET << "\n\n";
}

void TextReporter::print_workload_summary(const WorkloadModel& model, std::ostream& os) {
  os << BOLD << "═══ Workload Characteristics ═══════════════════════════════════════" << RESET << "\n";
  os << std::left << std::setw(30) << "  Total Allocations:"      << model.total_allocations << "\n";
  os << std::left << std::setw(30) << "  Total Deallocations:"    << model.total_deallocations << "\n";
  os << std::left << std::setw(30) << "  Live At End:"            << model.unmatched_allocations << "\n";
  os << std::left << std::setw(30) << "  Total Requested Memory:"
     << (static_cast<double>(model.total_requested_bytes) / 1024.0 / 1024.0) << " MB\n";
  os << std::left << std::setw(30) << "  Distinct Allocation Sites:" << model.sites.size() << "\n";
  os << std::left << std::setw(30) << "  Active Threads:"         << model.distinct_threads << "\n";
  os << std::left << std::setw(30) << "  Mean Allocation Rate:"   << std::fixed << std::setprecision(1) << model.mean_alloc_rate_per_s << " ops/sec\n";
  os << "\n";

  os << BOLD << "─── Top Allocation Sites (by volume) ──────────────────────────────" << RESET << "\n";
  os << "  " << std::left << std::setw(18) << "Site Hash"
     << std::setw(12) << "Allocs"
     << std::setw(14) << "Mean Size"
     << std::setw(16) << "Mean Lifetime"
     << std::setw(14) << "Dist Type" << "\n";
  os << "  ──────────────────────────────────────────────────────────────────\n";

  size_t count = 0;
  for (uint64_t hash : model.sites_by_bytes) {
    if (++count > 8) break;
    const auto* p = model.site(hash);
    if (!p) continue;

    std::string dist_name = "Unknown";
    switch (p->lifetime.distribution_type) {
      case DistributionType::Exponential: dist_name = "Exponential"; break;
      case DistributionType::Normal:      dist_name = "Normal"; break;
      case DistributionType::Bimodal:     dist_name = "Bimodal"; break;
      case DistributionType::HeavyTailed: dist_name = "Heavy-Tailed"; break;
      default: break;
    }

    std::stringstream hex_hash;
    hex_hash << "0x" << std::hex << hash;

    os << "  " << std::left << std::setw(18) << hex_hash.str()
       << std::setw(12) << p->total_allocations
       << std::setw(14) << (std::to_string(static_cast<int>(p->size_dist.mean_bytes)) + " B")
       << std::setw(16) << (std::to_string(static_cast<int>(p->lifetime.mean_ns / 1000.0)) + " µs")
       << std::setw(14) << dist_name << "\n";
  }
  os << "\n";
}

void TextReporter::print_fragmentation_analysis(const FragmentationAnalysisResult& frag, std::ostream& os) {
  os << BOLD << "═══ Fragmentation Diagnostics ══════════════════════════════════════" << RESET << "\n";
  os << "  Aggregate Internal Waste: "
     << (frag.internal.aggregate_waste_fraction > 0.20 ? RED : GREEN)
     << std::fixed << std::setprecision(2) << (frag.internal.aggregate_waste_fraction * 100.0) << "%"
     << RESET << " (" << (static_cast<double>(frag.internal.total_wasted_bytes)
                            / 1024.0 / 1024.0) << " MB)\n\n";

  if (frag.diagnostics.empty()) {
    os << "  " << GREEN << "✔ No critical fragmentation patterns detected." << RESET << "\n\n";
    return;
  }

  for (size_t i = 0; i < frag.diagnostics.size(); ++i) {
    const auto& d = frag.diagnostics[i];
    os << "  [" << (i + 1) << "] " << BOLD << YELLOW;
    switch (d.type) {
      case FragmentationType::LifetimeMixing: os << "LIFETIME MIXING DETECTED"; break;
      case FragmentationType::Churn:          os << "HIGH-CHURN BURST DETECTED"; break;
      case FragmentationType::Internal:       os << "INTERNAL PADDING OVERHEAD"; break;
      default: os << "FRAGMENTATION BOTTLENECK"; break;
    }
    os << RESET << " (Severity: " << std::fixed << std::setprecision(2) << d.severity << ")\n";
    os << "      Issue: " << d.description << "\n";
    os << "      " << GREEN << "Recommendation:" << RESET << " " << d.recommendation << "\n\n";
  }
}

void TextReporter::print_optimization_result(const OptimizationResult& result, std::ostream& os) {
  os << BOLD << "═══ APM Optimization Summary ═══════════════════════════════════════" << RESET << "\n";
  os << "  Candidates Evaluated: " << result.candidates_evaluated
     << " in " << result.search_duration_ms << " ms\n";
  os << "  Baseline Cost:        " << std::fixed << std::setprecision(4)
     << result.baseline.metrics.cost_value << "\n\n";

  os << "  " << std::left << std::setw(25) << "Strategy"
     << std::setw(12) << "Cost J(P)"
     << std::setw(15) << "Improvement"
     << std::setw(12) << "Constraints" << "\n";
  os << "  ────────────────────────────────────────────────────────────────\n";

  for (const auto& cand : result.ranked_candidates) {
    bool is_best = result.best_candidate && (result.best_candidate->strategy_name == cand.strategy_name);
    double imp = cand.evaluation.improvement_vs_baseline * 100.0;

    os << (is_best ? GREEN : "") << "  "
       << std::left << std::setw(25) << cand.strategy_name
       << std::setw(12) << std::fixed << std::setprecision(4) << cand.evaluation.metrics.cost_value
       << std::setw(15) << ((imp >= 0 ? "+" : "") + std::to_string(imp).substr(0, 5) + "%")
       << std::setw(12) << (cand.evaluation.passes_constraints ? "PASS" : "FAIL")
       << (is_best ? " ★ BEST" : "")
       << RESET << "\n";
  }
  os << "\n";

  if (result.best_candidate) {
    os << GREEN << BOLD << "  ✔ Recommended Policy: " << result.best_candidate->strategy_name << RESET << "\n";
    os << "    " << result.best_candidate->policy.description << "\n\n";
  }
}

void TextReporter::print_policy_details(const AllocationPolicy& policy, std::ostream& os) {
  os << BOLD << "═══ Policy Details: " << policy.name << " ═════════════════════════" << RESET << "\n";
  os << "  Strategy:    " << policy.strategy << "\n";
  os << "  Description: " << policy.description << "\n";
  os << "  Arenas (" << policy.arenas.size() << "):\n";

  for (const auto& a : policy.arenas) {
    os << "    • [Arena " << a.id << "] " << a.config.label
       << " (" << a.rationale << ")\n";
  }
  os << "  Assigned Sites: " << policy.arena_assignments.size() << "\n\n";
}

void TextReporter::print_comparison(const SimulatedMetrics& base, const SimulatedMetrics& opt, std::ostream& os) {
  os << BOLD << "═══ Side-by-Side Comparison ═══════════════════════════════════════" << RESET << "\n";
  os << "  " << std::left << std::setw(28) << "Metric"
     << std::setw(16) << "Baseline"
     << std::setw(16) << "FragmentX Opt"
     << std::setw(12) << "Delta" << "\n";
  os << "  ───────────────────────────────────────────────────────────────────\n";

  auto print_row = [&](const std::string& name, double b, double o, const std::string& unit, bool lower_better) {
    double delta = b > 0 ? ((o - b) / b) * 100.0 : 0.0;
    bool improved = lower_better ? (o < b) : (o > b);
    os << "  " << std::left << std::setw(28) << name
       << std::setw(16) << (std::to_string(b).substr(0, 6) + " " + unit)
       << std::setw(16) << (std::to_string(o).substr(0, 6) + " " + unit)
       << (improved ? GREEN : YELLOW)
       << ((delta >= 0 ? "+" : "") + std::to_string(delta).substr(0, 5) + "%")
       << RESET << "\n";
  };

  print_row("Estimated Memory Waste",
            static_cast<double>(base.estimated_memory_overhead_bytes) / 1024.0,
            static_cast<double>(opt.estimated_memory_overhead_bytes) / 1024.0,
            "KB", true);
  print_row("Estimated Latency Index", base.estimated_latency_score, opt.estimated_latency_score, "score", true);
  print_row("Contention Index", base.estimated_contention_score, opt.estimated_contention_score, "score", true);
  print_row("Cost Function J(P)", base.cost_value, opt.cost_value, "", true);
  os << "\n";
}

} // namespace cli
} // namespace fragmentx

