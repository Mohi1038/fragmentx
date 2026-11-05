/// @file lifetime_model.cpp
#include <fragmentx/lifetime.hpp>
#include <fragmentx/workload.hpp>
#include <unordered_map>

namespace fragmentx {

// LifetimeModel is populated by WorkloadModelBuilder; here we provide
// out-of-line implementations for the interface methods.

LifetimeEstimate LifetimeModel::estimate_for(uint64_t /*site_hash*/) const noexcept {
  return LifetimeEstimate{};
}

DistributionType LifetimeModel::dominant_distribution() const noexcept {
  return DistributionType::Unknown;
}

std::size_t LifetimeModel::site_count() const noexcept {
  return 0;
}

} // namespace fragmentx

