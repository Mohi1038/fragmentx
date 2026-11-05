#pragma once

/// @file optimizer.hpp
/// @brief APM (Adaptive Placement Model) optimizer API.
///
/// The APM optimizer reads a WorkloadModel, computes allocation-site
/// compatibility scores, generates candidate allocation policies, evaluates
/// each candidate via trace replay, and returns a ranked result set.
///
/// @note APM is a research-driven algorithm. Its effectiveness must be
///       validated against baseline allocators on each specific workload.
///       See docs/algorithms/apm.md for the full methodology.

#include <fragmentx/workload.hpp>
#include <fragmentx/policy.hpp>
#include <fragmentx/capture.hpp>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <optional>

namespace fragmentx {

// ─── Feature vector ─────────────────────────────────────────────────────────

/// @brief Feature vector Xi for a single allocation site.
///
/// Encodes the multi-signal behavioral profile used by the APM compatibility
/// estimator. Each component is normalized to [0, 1] before scoring.
///
/// Xi = [Si, Li, σ_Li, Fi, Bi, Ti, Ri]
struct FeatureVector {
  uint64_t site_hash       = 0;
  double   mean_size       = 0.0;  ///< Si: mean allocation size (bytes).
  double   size_cv         = 0.0;  ///< Si variance: coefficient of variation.
  double   mean_lifetime   = 0.0;  ///< Li: mean lifetime (ns).
  double   lifetime_cv     = 0.0;  ///< σ_Li: lifetime variance (CV).
  double   alloc_frequency = 0.0;  ///< Fi: allocations per second.
  double   burstiness      = 0.0;  ///< Bi: IAT coefficient of variation [0,1].
  double   thread_entropy  = 0.0;  ///< Ti: Shannon entropy of thread distribution.
  double   reuse_rate      = 0.0;  ///< Ri: address reuse fraction [0,1].
};

// ─── Compatibility ──────────────────────────────────────────────────────────

/// @brief Individual compatibility components between two allocation sites.
struct CompatibilityComponents {
  double size_compat     = 0.0; ///< Size similarity.
  double lifetime_compat = 0.0; ///< Lifetime distribution overlap.
  double phase_compat    = 0.0; ///< Temporal co-occurrence.
  double thread_compat   = 0.0; ///< Thread-set Jaccard similarity.
  double reuse_compat    = 0.0; ///< Reuse pattern compatibility.
};

/// @brief Weighted compatibility score C(i, j) between two allocation sites.
///
/// C(i,j) = w1*C_size + w2*C_lifetime + w3*C_phase + w4*C_thread + w5*C_reuse
struct CompatibilityScore {
  uint64_t site_a            = 0;
  uint64_t site_b            = 0;
  double   total             = 0.0;  ///< Weighted total in [0, 1].
  CompatibilityComponents    components;
  bool     can_share_arena   = false; ///< total >= compatibility_threshold.
};

/// @brief Weights for the compatibility scoring function.
struct CompatibilityWeights {
  double w_size     = 0.20;
  double w_lifetime = 0.35; ///< Lifetime dominates — it most strongly predicts fragmentation.
  double w_phase    = 0.20;
  double w_thread   = 0.15;
  double w_reuse    = 0.10;

  /// Minimum total score for two sites to be considered compatible.
  double threshold  = 0.55;

  /// Validate that all weights sum to 1.0.
  [[nodiscard]] bool is_valid() const noexcept;
};

// ─── Policy evaluation ──────────────────────────────────────────────────────

/// @brief Cost function weights for policy evaluation.
///
/// J(P) = alpha*M(P) + beta*L(P) + gamma*Q(P) + delta*O(P)
struct CostWeights {
  double alpha = 0.40; ///< M(P): memory overhead / fragmentation.
  double beta  = 0.30; ///< L(P): allocation and deallocation latency estimate.
  double gamma = 0.20; ///< Q(P): contention / throughput penalty.
  double delta = 0.10; ///< O(P): operational metadata overhead.

  [[nodiscard]] bool is_valid() const noexcept {
    return std::abs(alpha + beta + gamma + delta - 1.0) < 1e-6;
  }
};

/// @brief Hard performance constraints that a policy must satisfy.
struct PolicyConstraints {
  /// Maximum acceptable increase in estimated allocation latency (fraction).
  /// E.g. 0.20 = at most 20% worse than baseline latency.
  double max_latency_increase   = 0.20;

  /// Minimum required improvement in memory efficiency (fraction).
  /// E.g. 0.0 = any improvement is acceptable; 0.05 = must save at least 5%.
  double min_memory_improvement = 0.0;

  /// Maximum metadata overhead as a fraction of live bytes.
  double max_metadata_overhead  = 0.05;
};

/// @brief Simulated metrics from replaying a trace under a candidate policy.
struct SimulatedMetrics {
  uint64_t estimated_memory_overhead_bytes = 0;
  double   estimated_latency_score         = 0.0;
  double   estimated_contention_score      = 0.0;
  uint64_t metadata_bytes                  = 0;
  double   cost_value                      = 0.0;  ///< J(P).
};

/// @brief Result of evaluating a single candidate policy.
struct EvaluationResult {
  SimulatedMetrics metrics;
  bool   passes_constraints = false;
  std::string violation_reason; ///< Non-empty when passes_constraints == false.
  double improvement_vs_baseline = 0.0; ///< Fraction improvement. Negative = regression.
};

// ─── Candidate policy ───────────────────────────────────────────────────────

/// @brief A candidate allocation policy produced by the optimizer.
struct CandidatePolicy {
  AllocationPolicy policy;
  std::string      strategy_name; ///< E.g. "CompatibilityGrouped".
  EvaluationResult evaluation;
};

// ─── Policy search configuration ────────────────────────────────────────────

/// @brief Configuration for the APM policy search.
struct PolicySearchConfig {
  CompatibilityWeights compat_weights;
  CostWeights          cost_weights;
  PolicyConstraints    constraints;

  /// Maximum number of candidate policies to generate and evaluate.
  uint32_t max_candidates = 10;

  /// Whether to run a refinement pass on the top-3 candidates.
  bool enable_refinement = true;

  /// Progress callback: invoked periodically during optimization.
  /// Argument is fraction complete in [0, 1].
  std::function<void(double progress)> progress_callback;
};

// ─── Optimization result ────────────────────────────────────────────────────

/// @brief Full output of the APM optimizer.
struct OptimizationResult {
  /// All evaluated candidates, ranked by cost (best first).
  std::vector<CandidatePolicy> ranked_candidates;

  /// Baseline evaluation (default policy, no optimization).
  EvaluationResult baseline;

  /// The recommended best candidate (== ranked_candidates.front() if non-empty).
  std::optional<CandidatePolicy> best_candidate;

  /// Wall-clock time the optimizer ran for (milliseconds).
  uint64_t search_duration_ms = 0;

  /// Total candidate policies evaluated.
  uint32_t candidates_evaluated = 0;

  /// @return true if at least one candidate improves on the baseline.
  [[nodiscard]] bool found_improvement() const noexcept {
    return best_candidate.has_value() &&
           best_candidate->evaluation.improvement_vs_baseline > 0.0;
  }
};

// ─── APMOptimizer ───────────────────────────────────────────────────────────

/// @brief The Adaptive Placement Model optimizer.
///
/// Takes a WorkloadModel and an AllocationDatabase, runs the APM algorithm,
/// and returns a ranked set of candidate allocation policies.
///
/// @code
/// fragmentx::APMOptimizer optimizer;
/// fragmentx::PolicySearchConfig cfg;
/// cfg.progress_callback = [](double p){ printf("%.0f%%\n", p*100); };
/// auto result = optimizer.optimize(workload, alloc_db, cfg);
/// if (result.found_improvement()) {
///   fragmentx::PolicySerializer::save(
///     result.best_candidate->policy, "best-policy.json");
/// }
/// @endcode
class APMOptimizer {
public:
  APMOptimizer() = default;

  /// @brief Run the APM policy search.
  /// @param workload   WorkloadModel produced by the analyzer.
  /// @param config     Search configuration and cost function weights.
  /// @return           Ranked set of candidate policies.
  [[nodiscard]] OptimizationResult
  optimize(const WorkloadModel& workload, const PolicySearchConfig& config) const;

  /// @brief Compute the feature vector for every allocation site.
  [[nodiscard]] std::vector<FeatureVector>
  extract_features(const WorkloadModel& workload) const;

  /// @brief Compute pairwise compatibility between two sites.
  [[nodiscard]] CompatibilityScore
  compute_compatibility(const FeatureVector& a,
                        const FeatureVector& b,
                        const CompatibilityWeights& weights) const noexcept;
};

} // namespace fragmentx

