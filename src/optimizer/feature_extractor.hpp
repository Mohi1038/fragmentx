#pragma once

#include <fragmentx/optimizer.hpp>
#include <fragmentx/workload.hpp>
#include <vector>
#include <unordered_map>

namespace fragmentx {
namespace optimizer {

class FeatureExtractor {
public:
  static std::vector<FeatureVector> extract(const WorkloadModel& workload);
};

} // namespace optimizer
} // namespace fragmentx

