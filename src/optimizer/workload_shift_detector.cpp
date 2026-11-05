#include "workload_shift_detector.hpp"
#include <cmath>
#include <algorithm>

namespace fragmentx {
namespace optimizer {

ShiftReport WorkloadShiftDetector::detect_shift(
    const WorkloadModel& baseline,
    const WorkloadModel& current,
    double threshold)
{
  ShiftReport report;
  if (baseline.sites.empty() || current.sites.empty()) {
    report.shift_score = 0.0;
    report.trigger_reoptimize = false;
    return report;
  }

  // 1. Allocation rate shift
  double rate_shift = 0.0;
  if (baseline.mean_alloc_rate_per_s > 0) {
    rate_shift = std::abs(current.mean_alloc_rate_per_s - baseline.mean_alloc_rate_per_s)
               / baseline.mean_alloc_rate_per_s;
  }
  rate_shift = std::min(1.0, rate_shift);

  // 2. Site overlap & lifetime shifts
  size_t common_sites = 0;
  double lifetime_diff_sum = 0.0;

  for (const auto& [hash, base_prof] : baseline.sites) {
    auto it = current.sites.find(hash);
    if (it != current.sites.end()) {
      common_sites++;
      const auto& curr_prof = it->second;
      if (base_prof.lifetime.mean_ns > 0) {
        double diff = std::abs(curr_prof.lifetime.mean_ns - base_prof.lifetime.mean_ns)
                    / base_prof.lifetime.mean_ns;
        lifetime_diff_sum += std::min(1.0, diff);
      }
    }
  }

  double site_retention = static_cast<double>(common_sites)
      / static_cast<double>(std::max(baseline.sites.size(), current.sites.size()));
  double site_shift = 1.0 - site_retention;

  double mean_lifetime_shift = (common_sites > 0) ? (lifetime_diff_sum / common_sites) : 1.0;

  // Composite shift score
  report.shift_score = 0.35 * rate_shift + 0.35 * site_shift + 0.30 * mean_lifetime_shift;
  report.shift_score = std::clamp(report.shift_score, 0.0, 1.0);

  if (report.shift_score > threshold) {
    report.trigger_reoptimize = true;
    report.reason = "Workload distribution shifted significantly (score: "
                  + std::to_string(report.shift_score).substr(0, 4)
                  + " > threshold " + std::to_string(threshold).substr(0, 4) + ")";
  } else {
    report.trigger_reoptimize = false;
    report.reason = "Workload matches active policy within tolerance.";
  }

  return report;
}

} // namespace optimizer
} // namespace fragmentx

