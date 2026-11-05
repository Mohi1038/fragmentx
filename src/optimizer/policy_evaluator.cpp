#include "policy_evaluator.hpp"
#include <algorithm>
#include <cmath>

namespace fragmentx {
namespace optimizer {

SimulatedMetrics PolicyEvaluator::simulate(
    const AllocationPolicy& policy,
    const WorkloadModel& model,
    const CostWeights& weights)
{
  SimulatedMetrics m;
  uint64_t total_bytes = model.total_requested_bytes > 0 ? model.total_requested_bytes : 1;
  const uint64_t arena_count = static_cast<uint64_t>(policy.arenas.size());

  // Memory overhead calculation
  // Grouping by lifetime or size reduces fragmentation
  double frag_reduction = 0.0;
  if (policy.strategy == "LifetimeGrouped") {
    frag_reduction = 0.35; // 35% less fragmentation from segregating lifetimes
  } else if (policy.strategy == "CompatibilityGrouped") {
    frag_reduction = 0.40; // 40% less fragmentation
  } else if (policy.strategy == "SizeClassOptimized") {
    frag_reduction = 0.25; // 25% less internal waste
  } else if (policy.strategy == "ThreadAffinity") {
    frag_reduction = 0.15;
  }

  double base_frag_fraction = 0.15;
  double effective_frag = base_frag_fraction * (1.0 - frag_reduction);
  m.estimated_memory_overhead_bytes = static_cast<uint64_t>(total_bytes * effective_frag);

  // Metadata overhead
  m.metadata_bytes = arena_count * 4096 + policy.arena_assignments.size() * 32;

  // Latency score: more dedicated arenas = better cache locality, lower lock contention
  double base_latency = 1.0;
  double locality_bonus = std::min(0.25, (arena_count - 1) * 0.08);
  m.estimated_latency_score = base_latency - locality_bonus;

  // Contention score: thread-affinity and segregated arenas reduce contention significantly
  double contention_base = 1.0;
  if (policy.strategy == "ThreadAffinity") {
    contention_base = 0.30;
  } else if (arena_count > 1) {
    contention_base = std::max(0.40, 1.0 - (arena_count * 0.15));
  }
  m.estimated_contention_score = contention_base;

  // Multi-objective cost J(P) = alpha*M + beta*L + gamma*Q + delta*O
  double M = static_cast<double>(m.estimated_memory_overhead_bytes) / static_cast<double>(total_bytes);
  double O = static_cast<double>(m.metadata_bytes) / static_cast<double>(total_bytes);

  m.cost_value = weights.alpha * M
               + weights.beta  * m.estimated_latency_score
               + weights.gamma * m.estimated_contention_score
               + weights.delta * O;

  return m;
}

EvaluationResult PolicyEvaluator::evaluate(
    const AllocationPolicy& policy,
    const WorkloadModel& model,
    const CostWeights& weights,
    const PolicyConstraints& constraints,
    double baseline_cost,
    double baseline_latency)
{
  EvaluationResult res;
  res.metrics = simulate(policy, model, weights);

  if (baseline_cost > 0) {
    res.improvement_vs_baseline = (baseline_cost - res.metrics.cost_value) / baseline_cost;
  }

  bool ok = true;
  if (baseline_latency > 0 &&
      res.metrics.estimated_latency_score > baseline_latency * (1.0 + constraints.max_latency_increase)) {
    ok = false;
    res.violation_reason = "Latency increase exceeds limit (" +
        std::to_string(constraints.max_latency_increase * 100) + "%)";
  }

  uint64_t total_bytes = model.total_requested_bytes > 0 ? model.total_requested_bytes : 1;
  double meta_ratio = static_cast<double>(res.metrics.metadata_bytes) / static_cast<double>(total_bytes);
  if (meta_ratio > constraints.max_metadata_overhead) {
    ok = false;
    res.violation_reason = "Metadata overhead exceeds maximum allowed threshold.";
  }

  res.passes_constraints = ok;
  return res;
}

} // namespace optimizer
} // namespace fragmentx

