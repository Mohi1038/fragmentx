#pragma once

/// @file workload.hpp
/// @brief Workload model types produced by the FragmentX analyzer.

#include <fragmentx/lifetime.hpp>
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <string>

namespace fragmentx {

// ─── SizeDistribution ───────────────────────────────────────────────────────

/// @brief Summary statistics for a distribution of allocation sizes.
struct SizeDistribution {
  double   mean_bytes   = 0.0;    ///< Mean requested allocation size.
  double   stddev_bytes = 0.0;    ///< Standard deviation of requested sizes.
  double   cv           = 0.0;    ///< Coefficient of variation (stddev / mean).
  uint64_t min_bytes    = 0;      ///< Smallest observed allocation.
  uint64_t max_bytes    = 0;      ///< Largest observed allocation.
  uint64_t p50_bytes    = 0;
  uint64_t p95_bytes    = 0;
  uint64_t p99_bytes    = 0;

  /// Histogram: counts[i] = number of allocations in exponential bucket i.
  /// Bucket boundaries: [2^i, 2^(i+1)) bytes, i = 0..31.
  std::array<uint64_t, 32> histogram = {};
};

// ─── AllocationSiteProfile ──────────────────────────────────────────────────

/// @brief Full behavioral profile for a single allocation call site.
struct AllocationSiteProfile {
  /// Call site identifier.
  uint64_t site_hash = 0;

  /// Resolved source location (file:line:function). May be empty if
  /// symbol resolution was not performed or failed.
  std::string source_location;

  // ── Volume ──────────────────────────────────────────────────────────

  uint64_t total_allocations    = 0; ///< Total allocate() calls from this site.
  uint64_t total_bytes          = 0; ///< Total bytes requested.
  uint64_t live_allocations     = 0; ///< Objects still alive at trace end.
  uint64_t peak_concurrent_live = 0; ///< Max concurrent live objects from this site.
  uint64_t peak_live_bytes      = 0; ///< Max concurrent live bytes from this site.

  // ── Size distribution ───────────────────────────────────────────────

  SizeDistribution size_dist;

  // ── Lifetime ────────────────────────────────────────────────────────

  LifetimeEstimate lifetime;

  // ── Temporal patterns ───────────────────────────────────────────────

  /// Burstiness score [0, 1].
  /// 0 = allocations arrive at a perfectly uniform rate.
  /// 1 = all allocations occur in a single instantaneous burst.
  /// Computed from the coefficient of variation of inter-arrival times.
  double burstiness = 0.0;

  /// Timestamps (ns) of detected allocation burst midpoints.
  std::vector<uint64_t> burst_timestamps_ns;

  // ── Threading ───────────────────────────────────────────────────────

  /// Number of distinct threads that allocated from this site.
  uint32_t thread_count = 0;

  /// Allocation count per thread ID.
  std::unordered_map<uint64_t, uint64_t> thread_distribution;

  /// Shannon entropy of the thread distribution.
  /// 0 = single-threaded site; high = uniformly spread across threads.
  double thread_entropy = 0.0;

  // ── Reuse ───────────────────────────────────────────────────────────

  /// Fraction of freed addresses from this site that were reallocated
  /// within 1ms. High values indicate allocator address reuse opportunity.
  double reuse_rate = 0.0;
};

// ─── PhaseInterval ──────────────────────────────────────────────────────────

/// @brief A time window with a distinct allocation pattern (a "phase").
struct PhaseInterval {
  uint64_t start_ns = 0;      ///< Phase start timestamp.
  uint64_t end_ns   = 0;      ///< Phase end timestamp.
  double   alloc_rate = 0.0;  ///< Mean allocations/second during this phase.
  double   mean_size  = 0.0;  ///< Mean allocation size during this phase.

  /// Sites most active during this phase.
  std::vector<uint64_t> dominant_sites;
};

// ─── SiteCorrelation ────────────────────────────────────────────────────────

/// @brief Correlation between two allocation sites.
struct SiteCorrelation {
  uint64_t site_a;         ///< First site hash.
  uint64_t site_b;         ///< Second site hash.
  double   correlation;    ///< Pearson correlation of their live-object timelines.
  bool     phase_aligned;  ///< True if they allocate/free in the same phases.
};

// ─── WorkloadModel ──────────────────────────────────────────────────────────

/// @brief Complete characterization of an application's allocation workload.
///
/// Produced by the analyzer from a parsed trace. The optimizer consumes this
/// model to generate candidate allocation policies.
struct WorkloadModel {
  // ── Global statistics ───────────────────────────────────────────────

  uint64_t total_events           = 0; ///< Total alloc + free events.
  uint64_t total_allocations      = 0;
  uint64_t total_deallocations    = 0;
  uint64_t unmatched_allocations  = 0; ///< Live objects at trace end.
  uint64_t unmatched_frees        = 0; ///< Potential double-frees (warning).

  uint64_t total_requested_bytes  = 0;
  uint64_t peak_live_bytes        = 0;
  uint64_t peak_live_objects      = 0;

  double   mean_alloc_rate_per_s  = 0.0;
  double   overall_burstiness     = 0.0;

  uint32_t distinct_threads       = 0;
  uint64_t trace_duration_ns      = 0;

  // ── Per-site profiles ───────────────────────────────────────────────

  /// Profile for each allocation site (keyed by site_hash).
  std::unordered_map<uint64_t, AllocationSiteProfile> sites;

  /// Sites sorted by total_bytes descending (hottest first).
  std::vector<uint64_t> sites_by_bytes;

  /// Sites sorted by total_allocations descending.
  std::vector<uint64_t> sites_by_count;

  // ── Temporal structure ──────────────────────────────────────────────

  /// Detected allocation phases.
  std::vector<PhaseInterval> phases;

  /// Statistically significant cross-site correlations.
  std::vector<SiteCorrelation> correlations;

  // ── Helpers ─────────────────────────────────────────────────────────

  /// @return Site profile for the given hash, or nullptr if not found.
  [[nodiscard]] const AllocationSiteProfile*
  site(uint64_t hash) const noexcept {
    auto it = sites.find(hash);
    return it != sites.end() ? &it->second : nullptr;
  }

  /// @return true if the workload has significant phase structure.
  [[nodiscard]] bool is_phase_structured() const noexcept {
    return phases.size() >= 2;
  }

  /// @return true if the workload has significant cross-site correlations.
  [[nodiscard]] bool has_correlated_sites() const noexcept {
    return !correlations.empty();
  }
};

} // namespace fragmentx

