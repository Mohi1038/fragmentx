#pragma once

/// @file statistics.hpp
/// @brief Statistics structures collected by the allocator, capture, and analyzer.

#include <cstdint>
#include <cstddef>
#include <array>

namespace fragmentx {

// ─── AllocatorStatistics ────────────────────────────────────────────────────

/// @brief Runtime statistics collected by the FragmentX allocator.
///
/// Retrieve via Allocator::statistics(). Values are snapshots; they do not
/// update automatically after the call.
struct AllocatorStatistics {
  // ── Allocation counts ──────────────────────────────────────────────────

  /// Total number of allocate() calls since allocator construction.
  uint64_t total_allocations    = 0;

  /// Total number of deallocate() calls since allocator construction.
  uint64_t total_deallocations  = 0;

  /// Total number of reallocate() calls since allocator construction.
  uint64_t total_reallocations  = 0;

  /// Total number of allocations served from the thread-local cache.
  uint64_t cache_hits           = 0;

  /// Total number of allocations that required a global arena lookup.
  uint64_t cache_misses         = 0;

  /// Total number of allocations that fell back to the system allocator.
  uint64_t fallback_allocations = 0;

  /// Number of times a new region was acquired from the OS.
  uint64_t regions_acquired     = 0;

  /// Number of regions returned to the OS.
  uint64_t regions_released     = 0;

  // ── Byte counters ──────────────────────────────────────────────────────

  /// Total bytes requested by the application across all allocate() calls.
  uint64_t requested_bytes      = 0;

  /// Total bytes actually reserved by the allocator (requested + size-class padding).
  uint64_t reserved_bytes       = 0;

  /// Bytes currently in use (allocated but not freed).
  uint64_t live_bytes           = 0;

  /// Total bytes reserved across all FragmentX regions from the OS.
  uint64_t region_bytes_total   = 0;

  /// Total bytes committed (backed by physical pages) across all regions.
  uint64_t region_bytes_committed = 0;

  /// Bytes held in allocator-internal free lists (freed but not returned to OS).
  uint64_t free_list_bytes      = 0;

  /// Bytes used by allocator metadata (headers, bitmaps, etc.).
  uint64_t metadata_bytes       = 0;

  // ── Fragmentation estimates ────────────────────────────────────────────

  /// Internal fragmentation: (reserved_bytes - requested_bytes) / reserved_bytes.
  /// Measured as a fraction in [0, 1].
  double internal_fragmentation_ratio = 0.0;

  // ── Arena statistics ──────────────────────────────────────────────────

  /// Number of active arenas.
  uint32_t active_arenas        = 0;

  /// Number of active thread-local caches.
  uint32_t active_thread_caches = 0;
};

// ─── CaptureStatistics ──────────────────────────────────────────────────────

/// @brief Statistics from an active or completed CaptureSession.
struct CaptureStatistics {
  /// Total allocation events recorded.
  uint64_t alloc_events_recorded  = 0;

  /// Total deallocation events recorded.
  uint64_t free_events_recorded   = 0;

  /// Events dropped due to ring buffer overflow.
  uint64_t events_dropped         = 0;

  /// Total bytes written to the trace file.
  uint64_t trace_bytes_written    = 0;

  /// Estimated per-allocation capture overhead in nanoseconds.
  double   capture_overhead_ns    = 0.0;

  /// Number of distinct allocation sites observed.
  uint64_t distinct_sites         = 0;

  /// Number of distinct threads observed.
  uint32_t distinct_threads       = 0;

  /// Capture duration in milliseconds.
  uint64_t capture_duration_ms    = 0;
};

// ─── FragmentationReport ────────────────────────────────────────────────────

/// @brief A fragmentation snapshot at a particular point in time or analysis.
struct FragmentationReport {
  // ── Memory breakdown ──────────────────────────────────────────────────

  /// Bytes currently requested by the application (live objects only).
  uint64_t requested_live_bytes   = 0;

  /// Bytes reserved by the allocator for live objects (includes size-class padding).
  uint64_t reserved_live_bytes    = 0;

  /// Bytes in allocator free lists (freed by application, held by allocator).
  uint64_t free_list_bytes        = 0;

  /// Bytes in regions committed from OS but not yet allocated.
  uint64_t uncommitted_arena_bytes = 0;

  /// Total bytes in regions acquired from OS.
  uint64_t total_region_bytes     = 0;

  /// Allocator metadata overhead.
  uint64_t metadata_bytes         = 0;

  // ── Fragmentation metrics ─────────────────────────────────────────────

  /// Internal fragmentation: (reserved_live - requested_live) / reserved_live.
  /// Formula: F_internal = (allocated_capacity - requested_bytes) / allocated_capacity
  double internal_fragmentation   = 0.0;

  /// Fraction of free-list space that is fragmented into blocks too small
  /// to satisfy typical allocation requests. Approximate.
  double external_fragmentation   = 0.0;

  /// Fraction of reserved region bytes that are free but retained by the allocator.
  double retention_ratio          = 0.0;

  // ── Latency ───────────────────────────────────────────────────────────

  /// Allocation latency at the 50th percentile (nanoseconds). -1 if unavailable.
  double alloc_latency_p50_ns     = -1.0;

  /// Allocation latency at the 95th percentile (nanoseconds). -1 if unavailable.
  double alloc_latency_p95_ns     = -1.0;

  /// Allocation latency at the 99th percentile (nanoseconds). -1 if unavailable.
  double alloc_latency_p99_ns     = -1.0;

  /// Peak Resident Set Size observed. 0 if unmeasured.
  uint64_t peak_rss_bytes         = 0;

  // ── Measurement transparency ───────────────────────────────────────────

  /// Human-readable description of how each metric was measured.
  /// FragmentX is explicit about what is simulated vs. directly measured.
  static const char* measurement_methodology() noexcept;
};

} // namespace fragmentx

