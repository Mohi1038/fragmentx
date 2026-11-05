#pragma once

#include <fragmentx/optimizer.hpp>
#include <vector>
#include <unordered_map>

namespace fragmentx {
namespace optimizer {

class CompatibilityEngine {
public:
  static CompatibilityScore score_pair(
      const FeatureVector& a,
      const FeatureVector& b,
      const CompatibilityWeights& weights) noexcept;

  static std::vector<CompatibilityScore> compute_all_pairs(
      const std::vector<FeatureVector>& features,
      const CompatibilityWeights& weights);
};

} // namespace optimizer
} // namespace fragmentx

