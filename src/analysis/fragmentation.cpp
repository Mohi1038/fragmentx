/// @file fragmentation.cpp
/// @brief Fragmentation analysis implementation.

#include "fragmentation.hpp"
#include <fragmentx/fragmentation.hpp>
#include <fragmentx/workload.hpp>
#include "trace_parser.hpp"
#include "../runtime/size_classes.hpp"
#include <algorithm>
#include <cmath>

namespace fragmentx {
namespace analysis {

FragmentationAnalysisResult FragmentationAnalyzer::analyze(
    const WorkloadModel& model,
    const AllocationDatabase& db)
{
  FragmentationAnalysisResult result;

  // ── Internal fragmentation ────────────────────────────────────────────────
  // Estimate FragmentX's size-class padding from observed allocation sizes.
  uint64_t total_requested = 0;
  uint64_t total_allocated = 0;

  constexpr std::size_t N_CLASSES = runtime::NUM_SIZE_CLASSES;
  constexpr auto& CLASS_SIZES = runtime::SIZE_CLASS_SIZES;

  struct ClassAccum { uint64_t count=0, req=0, alloc=0; };
  ClassAccum accum[N_CLASSES + 1]; // +1 for large

  for (const auto& event : db.events) {
    const uint32_t class_index = runtime::SizeClassManager::class_index(
        static_cast<std::size_t>(event.requested_size));
    const std::size_t allocated_size = class_index < N_CLASSES
        ? CLASS_SIZES[class_index]
        : static_cast<std::size_t>(event.requested_size);
    const std::size_t index = class_index < N_CLASSES ? class_index : N_CLASSES;
    ++accum[index].count;
    accum[index].req += event.requested_size;
    accum[index].alloc += allocated_size;
    total_requested += event.requested_size;
    total_allocated += allocated_size;
  }

  for (std::size_t i = 0; i <= N_CLASSES; ++i) {
    if (!accum[i].alloc) continue;
    InternalFragReport::SizeClassEntry e;
    e.size_class       = i < N_CLASSES ? CLASS_SIZES[i] : 0;
    e.allocation_count = accum[i].count;
    e.requested_bytes  = accum[i].req;
    e.allocated_bytes  = accum[i].alloc;
    e.waste_fraction   = (accum[i].alloc > accum[i].req)
        ? static_cast<double>(accum[i].alloc - accum[i].req)
          / static_cast<double>(accum[i].alloc)
        : 0.0;
    result.internal.by_size_class.push_back(e);
  }

  if (total_allocated >= total_requested && total_allocated > 0) {
    result.internal.aggregate_waste_fraction =
        static_cast<double>(total_allocated - total_requested)
      / static_cast<double>(total_allocated);
    result.internal.total_wasted_bytes = total_allocated - total_requested;
  }

  // ── Lifetime mixing diagnostics ──────────────────────────────────────────
  // Find sites where short-lived and long-lived objects coexist.
  std::vector<uint64_t> short_sites, long_sites;
  for (auto& [hash, p] : model.sites) {
    if (p.lifetime.mean_ns < 1'000'000ULL) // < 1ms = short
      short_sites.push_back(hash);
    else
      long_sites.push_back(hash);
  }

  if (!short_sites.empty() && !long_sites.empty()) {
    FragmentationDiagnostic diag;
    diag.type = FragmentationType::LifetimeMixing;
    diag.severity = std::min(1.0,
        static_cast<double>(std::min(short_sites.size(), long_sites.size())) /
        static_cast<double>(model.sites.size()));
    diag.description =
        "Short-lived and long-lived objects are allocated from the same regions. "
        "When short-lived objects are freed, they leave holes that prevent "
        "efficient region reclamation while long-lived objects remain.";
    diag.recommendation =
        "Assign short-lived and long-lived allocation sites to separate arenas. "
        "FragmentX optimizer strategy 'LifetimeGrouped' addresses this pattern.";
    diag.implicated_sites = short_sites;
    for (auto h : long_sites) diag.implicated_sites.push_back(h);
    result.diagnostics.push_back(std::move(diag));
  }

  // ── High-churn sites ─────────────────────────────────────────────────────
  for (auto& [hash, p] : model.sites) {
    if (p.burstiness > 0.8 && p.total_allocations > 1000) {
      FragmentationDiagnostic diag;
      diag.type = FragmentationType::Churn;
      diag.severity = p.burstiness;
      diag.description = "Allocation site 0x" + std::to_string(hash)
          + " shows high burstiness (" + std::to_string(p.burstiness).substr(0,4)
          + "), creating many short-lived free holes.";
      diag.recommendation =
          "Consider a pool allocator for fixed-size objects from this site, "
          "or a bump-pointer arena if objects are freed together.";
      diag.implicated_sites = {hash};
      diag.estimated_wasted_bytes = static_cast<uint64_t>(
          static_cast<double>(p.total_bytes) * result.internal.aggregate_waste_fraction);
      result.diagnostics.push_back(std::move(diag));
    }
  }

  // Sort diagnostics by severity.
  std::sort(result.diagnostics.begin(), result.diagnostics.end(),
      [](const FragmentationDiagnostic& a, const FragmentationDiagnostic& b) {
        return a.severity > b.severity;
      });

  return result;
}

} // namespace analysis

// ─── FragmentationAnalysisResult helpers ────────────────────────────────────

FragmentationType FragmentationAnalysisResult::dominant_type() const noexcept {
  if (diagnostics.empty()) return FragmentationType::Internal;
  return diagnostics.front().type;
}

double FragmentationAnalysisResult::aggregate_severity() const noexcept {
  if (diagnostics.empty()) return 0.0;
  double sum = 0.0;
  for (auto& d : diagnostics) sum += d.severity;
  return sum / static_cast<double>(diagnostics.size());
}

} // namespace fragmentx

