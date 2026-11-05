#include "feature_extractor.hpp"

namespace fragmentx {
namespace optimizer {

std::vector<FeatureVector> FeatureExtractor::extract(const WorkloadModel& workload) {
  std::vector<FeatureVector> features;
  features.reserve(workload.sites.size());

  for (const auto& [hash, profile] : workload.sites) {
    FeatureVector fv;
    fv.site_hash       = hash;
    fv.mean_size       = profile.size_dist.mean_bytes;
    fv.size_cv         = profile.size_dist.cv;
    fv.mean_lifetime   = profile.lifetime.mean_ns;
    fv.lifetime_cv     = profile.lifetime.cv;
    fv.alloc_frequency = static_cast<double>(profile.total_allocations);
    fv.burstiness      = profile.burstiness;
    fv.thread_entropy  = profile.thread_entropy;
    fv.reuse_rate      = profile.reuse_rate;
    features.push_back(fv);
  }
  return features;
}

} // namespace optimizer
} // namespace fragmentx

