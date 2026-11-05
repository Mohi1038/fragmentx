#include "compatibility.hpp"
#include <cmath>
#include <algorithm>

namespace fragmentx {

bool CompatibilityWeights::is_valid() const noexcept {
  const double sum = w_size + w_lifetime + w_phase + w_thread + w_reuse;
  return std::abs(sum - 1.0) < 1e-4;
}

namespace optimizer {

CompatibilityScore CompatibilityEngine::score_pair(
    const FeatureVector& a,
    const FeatureVector& b,
    const CompatibilityWeights& w) noexcept
{
  CompatibilityScore score;
  score.site_a = a.site_hash;
  score.site_b = b.site_hash;

  // Size compatibility
  double size_ratio = (a.mean_size > 0 && b.mean_size > 0)
      ? std::abs(std::log(a.mean_size / b.mean_size)) / std::log(512.0)
      : 1.0;
  score.components.size_compat = std::max(0.0, 1.0 - size_ratio);

  // Lifetime compatibility (log-scale difference)
  double lt_ratio = (a.mean_lifetime > 0 && b.mean_lifetime > 0)
      ? std::abs(std::log((a.mean_lifetime + 1.0) / (b.mean_lifetime + 1.0))) / std::log(1000.0)
      : 1.0;
  score.components.lifetime_compat = std::max(0.0, 1.0 - lt_ratio);

  // Phase compatibility
  double burst_diff = std::abs(a.burstiness - b.burstiness);
  score.components.phase_compat = std::max(0.0, 1.0 - burst_diff);

  // Thread compatibility
  double ent_diff = std::abs(a.thread_entropy - b.thread_entropy) / 10.0;
  score.components.thread_compat = std::max(0.0, 1.0 - ent_diff);

  // Reuse compatibility
  double reuse_diff = std::abs(a.reuse_rate - b.reuse_rate);
  score.components.reuse_compat = std::max(0.0, 1.0 - reuse_diff);

  score.total = w.w_size     * score.components.size_compat
              + w.w_lifetime * score.components.lifetime_compat
              + w.w_phase    * score.components.phase_compat
              + w.w_thread   * score.components.thread_compat
              + w.w_reuse    * score.components.reuse_compat;

  score.total = std::clamp(score.total, 0.0, 1.0);
  score.can_share_arena = (score.total >= w.threshold);
  return score;
}

std::vector<CompatibilityScore> CompatibilityEngine::compute_all_pairs(
    const std::vector<FeatureVector>& features,
    const CompatibilityWeights& weights)
{
  std::vector<CompatibilityScore> scores;
  const size_t n = features.size();
  scores.reserve((n * (n - 1)) / 2);

  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      scores.push_back(score_pair(features[i], features[j], weights));
    }
  }
  return scores;
}

} // namespace optimizer
} // namespace fragmentx

