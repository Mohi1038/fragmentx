#pragma once

/// @file lifetime.hpp
/// @brief Lifetime hint and estimation types for allocation-site classification.

#include <cstdint>
#include <cstddef>

namespace fragmentx {

// ─── LifetimeHint ───────────────────────────────────────────────────────────

/// @brief Caller-supplied hint about the expected lifetime of an allocation.
///
/// Hints allow developers to guide the allocator when the workload model is
/// not yet available or when precise placement is desired for critical paths.
/// The allocator treats hints as advisory; it may ignore them if the active
/// policy contradicts them.
enum class LifetimeHint : uint8_t {
  /// Caller has no information about expected lifetime. The allocator uses its
  /// workload model or default policy.
  Unknown     = 0,

  /// Object is expected to be freed within microseconds to a few milliseconds.
  /// The allocator may place this object in a high-churn arena or bump-pointer
  /// region that can be bulk-reclaimed.
  ShortLived  = 1,

  /// Object is expected to persist for the lifetime of a request or phase,
  /// typically milliseconds to seconds.
  MediumLived = 2,

  /// Object is expected to persist for the lifetime of the process or a
  /// significant fraction of it. The allocator should place it in a stable
  /// region to avoid fragmenting short-lived arenas.
  LongLived   = 3,
};

// ─── DistributionType ───────────────────────────────────────────────────────

/// @brief Statistical shape of an observed lifetime distribution.
///
/// The analyzer classifies each allocation site's lifetime distribution based
/// on coefficient of variation, skewness, and modal structure.
enum class DistributionType : uint8_t {
  Unknown,     ///< Insufficient data to classify.
  Exponential, ///< Most objects are short-lived; classic memory churn pattern.
  Normal,      ///< Lifetimes cluster around a mean (low variance).
  Bimodal,     ///< Two distinct lifetime populations (e.g. cache hits vs misses).
  HeavyTailed, ///< Most objects are short-lived but rare very long-lived outliers exist.
  Uniform,     ///< Lifetimes are roughly evenly distributed across the range.
};

// ─── LifetimeEstimate ───────────────────────────────────────────────────────

/// @brief Statistical estimate of an allocation site's lifetime distribution.
struct LifetimeEstimate {
  /// Mean lifetime in nanoseconds.
  double mean_ns       = 0.0;

  /// Standard deviation of lifetime in nanoseconds.
  double stddev_ns     = 0.0;

  /// 5th percentile lifetime in nanoseconds.
  double p5_ns         = 0.0;

  /// 25th percentile lifetime in nanoseconds.
  double p25_ns        = 0.0;

  /// Median (50th percentile) lifetime in nanoseconds.
  double p50_ns        = 0.0;

  /// 75th percentile lifetime in nanoseconds.
  double p75_ns        = 0.0;

  /// 95th percentile lifetime in nanoseconds.
  double p95_ns        = 0.0;

  /// 99th percentile lifetime in nanoseconds.
  double p99_ns        = 0.0;

  /// Minimum observed lifetime in nanoseconds.
  double min_ns        = 0.0;

  /// Maximum observed lifetime in nanoseconds.
  double max_ns        = 0.0;

  /// Coefficient of variation (stddev / mean). High CV indicates high variance.
  double cv            = 0.0;

  /// Confidence in the estimate, 0.0–1.0.
  /// Low confidence indicates insufficient sample count.
  double confidence    = 0.0;

  /// Number of lifetime samples used to compute this estimate.
  std::size_t sample_count = 0;

  /// Classified shape of the lifetime distribution.
  DistributionType distribution_type = DistributionType::Unknown;

  /// @return true if sufficient data was collected to produce a reliable estimate.
  [[nodiscard]] bool is_reliable() const noexcept {
    return sample_count >= 30 && confidence >= 0.5;
  }
};

// ─── LifetimeModel ──────────────────────────────────────────────────────────

/// @brief Summary of the lifetime characteristics for a set of allocation sites.
struct LifetimeModel {
  /// Estimated lifetime for a specific allocation site, identified by hash.
  /// @param site_hash  FNV-1a hash of the allocation call site.
  /// @return           Lifetime estimate, or a default-constructed estimate if
  ///                   the site is not in the model.
  [[nodiscard]] LifetimeEstimate estimate_for(uint64_t site_hash) const noexcept;

  /// @return The distribution type observed most frequently across all sites.
  [[nodiscard]] DistributionType dominant_distribution() const noexcept;

  /// @return Total number of allocation sites in the model.
  [[nodiscard]] std::size_t site_count() const noexcept;
};

} // namespace fragmentx

