#pragma once

/// @file arena.hpp
/// @brief Arena descriptor and configuration types.

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace fragmentx {

// ─── ArenaType ──────────────────────────────────────────────────────────────

/// @brief Allocation strategy employed by an arena.
enum class ArenaType : uint8_t {
  /// Fixed-size object pool: all allocations are the same size.
  /// Very fast allocation (O(1) pop from free list); no internal fragmentation.
  FixedPool,

  /// Bump-pointer region: allocations are served by advancing a pointer.
  /// Extremely fast; memory can only be freed by destroying the entire region.
  /// Ideal for phase-based workloads where all objects share a common lifetime.
  Region,

  /// General-purpose arena using segregated free lists per size class.
  /// Supports individual object deallocation at the cost of some overhead.
  GeneralPurpose,

  /// Long-lived object arena: low-churn, stable placement.
  /// Objects placed here are assumed to persist for a long time.
  /// Avoids mixing with short-lived objects to prevent fragmentation.
  LongLived,

  /// Fallback path: delegates to the system allocator (malloc/free).
  /// Used when FragmentX cannot serve a request from its own regions.
  Fallback,
};

// ─── ArenaConfig ────────────────────────────────────────────────────────────

/// @brief Configuration for creating a specific arena.
struct ArenaConfig {
  /// Arena strategy.
  ArenaType type = ArenaType::GeneralPurpose;

  /// For FixedPool: the size in bytes of each pooled object.
  std::size_t object_size = 0;

  /// Initial size of the arena's backing region in bytes.
  /// 0 = use the default from Config::region_size.
  std::size_t initial_size = 0;

  /// Maximum size this arena may grow to. 0 = unlimited.
  std::size_t max_size = 0;

  /// Thread affinity: if non-zero, this arena is preferred for the given thread.
  /// 0 = shared across all threads.
  uint32_t preferred_thread_id = 0;

  /// Human-readable label for diagnostics and policy inspection.
  std::string label;
};

// ─── ArenaDescriptor ────────────────────────────────────────────────────────

/// @brief Read-only descriptor for an existing arena (returned by ArenaManager).
struct ArenaDescriptor {
  /// Unique arena identifier within the allocator instance.
  uint32_t id = 0;

  /// Arena strategy type.
  ArenaType type = ArenaType::GeneralPurpose;

  /// Total bytes reserved for this arena from the OS.
  std::size_t reserved_bytes = 0;

  /// Bytes currently allocated to live objects in this arena.
  std::size_t used_bytes = 0;

  /// Bytes available in the arena's free lists.
  std::size_t free_list_bytes = 0;

  /// Total allocation events served by this arena.
  uint64_t total_allocations = 0;

  /// Total deallocation events processed by this arena.
  uint64_t total_deallocations = 0;

  /// Label (may be empty).
  std::string_view label;
};

// ─── ArenaStats ─────────────────────────────────────────────────────────────

/// @brief Aggregate statistics across all arenas in an allocator.
struct ArenaStats {
  uint32_t   total_arenas             = 0;
  uint32_t   active_arenas            = 0;
  std::size_t total_reserved_bytes    = 0;
  std::size_t total_used_bytes        = 0;
  std::size_t total_free_list_bytes   = 0;
  uint64_t   total_allocations        = 0;
  uint64_t   total_deallocations      = 0;
};

} // namespace fragmentx

