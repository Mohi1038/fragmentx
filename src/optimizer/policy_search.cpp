/// @file policy_search.cpp
/// @brief APMOptimizer: the top-level policy search orchestrator.

#include <fragmentx/optimizer.hpp>
#include <fragmentx/workload.hpp>
#include <fragmentx/policy.hpp>
#include "feature_extractor.hpp"
#include "compatibility.hpp"
#include "candidate_generator.hpp"
#include "policy_evaluator.hpp"

#include <chrono>
#include <algorithm>

namespace fragmentx {

std::vector<FeatureVector>
APMOptimizer::extract_features(const WorkloadModel& model) const {
  return optimizer::FeatureExtractor::extract(model);
}

CompatibilityScore APMOptimizer::compute_compatibility(
    const FeatureVector& a,
    const FeatureVector& b,
    const CompatibilityWeights& w) const noexcept
{
  return optimizer::CompatibilityEngine::score_pair(a, b, w);
}

OptimizationResult APMOptimizer::optimize(
    const WorkloadModel& model,
    const PolicySearchConfig& config) const
{
  OptimizationResult result;
  auto t0 = std::chrono::steady_clock::now();

  auto report_progress = [&](double p) {
    if (config.progress_callback) config.progress_callback(p);
  };

  report_progress(0.0);

  // 1. Extract feature vectors
  auto features = extract_features(model);
  report_progress(0.15);

  // 2. Evaluate Baseline
  auto baseline_policy = optimizer::CandidateGenerator::make_baseline();
  result.baseline.metrics = optimizer::PolicyEvaluator::simulate(
      baseline_policy, model, config.cost_weights);
  result.baseline.passes_constraints = true;
  result.baseline.improvement_vs_baseline = 0.0;
  report_progress(0.25);

  // 3. Generate candidate policies
  auto candidate_policies = optimizer::CandidateGenerator::generate_all(
      features, config.compat_weights);
  report_progress(0.50);

  // 4. Evaluate each candidate
  double baseline_cost = result.baseline.metrics.cost_value;
  double baseline_lat  = result.baseline.metrics.estimated_latency_score;

  size_t idx = 0;
  for (auto& pol : candidate_policies) {
    CandidatePolicy cp;
    cp.policy        = pol;
    cp.strategy_name = pol.strategy;

    cp.evaluation = optimizer::PolicyEvaluator::evaluate(
        pol, model, config.cost_weights, config.constraints,
        baseline_cost, baseline_lat);

    pol.cost_value = cp.evaluation.metrics.cost_value;
    pol.estimated_improvement = cp.evaluation.improvement_vs_baseline;
    pol.confidence = 0.85;

    result.ranked_candidates.push_back(std::move(cp));
    idx++;
    report_progress(0.50 + 0.40 * (static_cast<double>(idx) / candidate_policies.size()));
  }
  result.candidates_evaluated = static_cast<uint32_t>(candidate_policies.size());

  // 5. Rank candidates by cost value (ascending)
  std::sort(result.ranked_candidates.begin(), result.ranked_candidates.end(),
      [](const CandidatePolicy& a, const CandidatePolicy& b) {
        if (a.evaluation.passes_constraints != b.evaluation.passes_constraints)
          return a.evaluation.passes_constraints > b.evaluation.passes_constraints;
        return a.evaluation.metrics.cost_value < b.evaluation.metrics.cost_value;
      });

  auto t1 = std::chrono::steady_clock::now();
  result.search_duration_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

  report_progress(1.0);
  return result;
}

} // namespace fragmentx
