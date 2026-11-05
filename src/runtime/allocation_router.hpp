#pragma once
/// @file allocation_router.hpp
/// @brief Routes allocation requests to arenas based on the active policy.

#include <fragmentx/policy.hpp>
#include <fragmentx/lifetime.hpp>
#include "size_classes.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>

namespace fragmentx {
namespace runtime {

/// @brief Result of a routing decision.
struct RoutingDecision {
  uint32_t arena_id        = 0;         ///< Target arena ID.
  uint32_t size_class_idx  = 0;         ///< Size class index.
  std::size_t rounded_size = 0;         ///< Size after rounding to class.
  bool     use_fallback    = false;     ///< True: skip FragmentX arenas, use system malloc.
};

/// @brief Routes allocations to arenas using the active policy.
///
/// Routing priority:
///  1. If active policy has a site assignment for site_hash → use that arena.
///  2. If LifetimeHint is LongLived → use the designated long-lived arena.
///  3. Otherwise → route by size class to the default arena.
///  4. If no arena can serve the request → fallback.
class AllocationRouter {
public:
  /// @brief Set the arena to use for each routing category.
  void set_default_arena(uint32_t id)    noexcept { default_arena_id_    = id; }
  void set_longlived_arena(uint32_t id)  noexcept { longlived_arena_id_  = id; }
  void set_fallback_arena(uint32_t id)   noexcept { fallback_arena_id_   = id; }

  /// @brief Load a new policy. Thread-safe (swaps atomically via shared_ptr).
  void load_policy(const AllocationPolicy& policy);

  /// @brief Clear the active policy, reverting to default routing.
  void clear_policy() noexcept;

  /// @return true if a policy is currently loaded.
  [[nodiscard]] bool has_policy() const noexcept;

  /// @brief Compute a routing decision for the given allocation request.
  [[nodiscard]] RoutingDecision route(
      std::size_t  size,
      std::size_t  alignment,
      uint64_t     site_hash,
      LifetimeHint hint) const noexcept;

private:
  uint32_t default_arena_id_   = 1;
  uint32_t longlived_arena_id_ = 2;
  uint32_t fallback_arena_id_  = 3;

  // The policy is immutable once loaded; swapped atomically as a whole.
  std::shared_ptr<const AllocationPolicy> policy_;
  mutable std::mutex                       policy_mutex_;
};

} // namespace runtime
} // namespace fragmentx

