#pragma once

/// @file fragmentation.hpp
/// @brief Fragmentation analysis types and diagnostics.

#include <cstdint>
#include <string>
#include <vector>

namespace fragmentx {

// ─── FragmentationType ──────────────────────────────────────────────────────

/// @brief Specific kind of fragmentation or memory inefficiency.
enum class FragmentationType : uint8_t {
  /// Size-class padding wastes memory inside allocated blocks.
  /// Measured as: (allocated_capacity - requested_bytes) / allocated_capacity.
  Internal,

  /// Free memory exists but is scattered in blocks too small or misaligned
  /// for efficient reuse by larger allocations.
  External,

  /// Objects are freed at the logical level but remain in allocator free lists
  /// or OS pages that have not been returned to the system.
  Retention,

  /// Short-lived and long-lived objects are interleaved in the same heap
  /// region, preventing reclamation of the region after short-lived objects die.
  LifetimeMixing,

  /// High object churn creates many small free holes scattered through a region.
  Churn,
};

// ─── FragmentationDiagnostic ────────────────────────────────────────────────

/// @brief A specific fragmentation finding for one or more allocation sites.
struct FragmentationDiagnostic {
  FragmentationType type;

  /// Human-readable description of the fragmentation problem.
  std::string description;

  /// Suggested optimization strategy.
  std::string recommendation;

  /// Fragmentation severity score [0, 1]. 1 = maximum severity.
  double severity = 0.0;

  /// Estimated bytes wasted due to this fragmentation pattern.
  uint64_t estimated_wasted_bytes = 0;

  /// Allocation sites implicated in this finding (site_hash values).
  std::vector<uint64_t> implicated_sites;
};

// ─── InternalFragReport ─────────────────────────────────────────────────────

/// @brief Per-size-class internal fragmentation breakdown.
struct InternalFragReport {
  struct SizeClassEntry {
    std::size_t size_class;          ///< Size class boundary (bytes).
    uint64_t    allocation_count;    ///< Number of allocations in this class.
    uint64_t    requested_bytes;     ///< Total requested bytes in this class.
    uint64_t    allocated_bytes;     ///< Total allocated bytes in this class.
    double      waste_fraction;      ///< (allocated - requested) / allocated.
  };

  std::vector<SizeClassEntry> by_size_class;

  double aggregate_waste_fraction = 0.0;  ///< Overall internal fragmentation.
  uint64_t total_wasted_bytes     = 0;    ///< Total bytes wasted across all classes.
};

// ─── TemporalFragReport ─────────────────────────────────────────────────────

/// @brief Time-series analysis of fragmentation.
struct TemporalFragReport {
  struct Snapshot {
    uint64_t timestamp_ns;
    uint64_t live_bytes;        ///< Bytes in live objects.
    uint64_t free_hole_bytes;   ///< Estimated bytes in fragmented free holes.
    uint64_t region_bytes;      ///< Total bytes in allocator regions.
    double   frag_ratio;        ///< free_hole_bytes / region_bytes.
  };

  std::vector<Snapshot> timeline;
  double peak_frag_ratio    = 0.0;
  uint64_t peak_frag_time_ns = 0;
};

// ─── FragmentationAnalysisResult ────────────────────────────────────────────

/// @brief Full fragmentation analysis output.
struct FragmentationAnalysisResult {
  InternalFragReport  internal;
  TemporalFragReport  temporal;

  /// Top N fragmentation findings, sorted by severity descending.
  std::vector<FragmentationDiagnostic> diagnostics;

  /// @return The most severe fragmentation type found, or Internal if empty.
  [[nodiscard]] FragmentationType dominant_type() const noexcept;

  /// @return Aggregate severity score across all diagnostics.
  [[nodiscard]] double aggregate_severity() const noexcept;
};

} // namespace fragmentx

