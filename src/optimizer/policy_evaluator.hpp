#pragma once

#include <fragmentx/optimizer.hpp>
#include <fragmentx/policy.hpp>
#include <fragmentx/workload.hpp>

namespace fragmentx {
namespace optimizer {

class PolicyEvaluator {
public:
  static SimulatedMetrics simulate(
      const AllocationPolicy& policy,
      const WorkloadModel& model,
      const CostWeights& weights);

  static EvaluationResult evaluate(
      const AllocationPolicy& policy,
      const WorkloadModel& model,
      const CostWeights& weights,
      const PolicyConstraints& constraints,
      double baseline_cost,
      double baseline_latency);
};

} // namespace optimizer
} // namespace fragmentx

