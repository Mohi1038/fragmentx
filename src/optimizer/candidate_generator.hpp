#pragma once

#include <fragmentx/optimizer.hpp>
#include <fragmentx/policy.hpp>
#include <vector>

namespace fragmentx {
namespace optimizer {

class CandidateGenerator {
public:
  static std::vector<AllocationPolicy> generate_all(
      const std::vector<FeatureVector>& features,
      const CompatibilityWeights& weights);

  static AllocationPolicy make_baseline();
  static AllocationPolicy make_lifetime_grouped(const std::vector<FeatureVector>& features);
  static AllocationPolicy make_compatibility_grouped(
      const std::vector<FeatureVector>& features,
      const CompatibilityWeights& weights);
  static AllocationPolicy make_size_optimized(const std::vector<FeatureVector>& features);
  static AllocationPolicy make_thread_affinity(const std::vector<FeatureVector>& features);
};

} // namespace optimizer
} // namespace fragmentx

