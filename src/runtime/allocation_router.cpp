/// @file allocation_router.cpp
#include "allocation_router.hpp"
#include <mutex>

namespace fragmentx {
namespace runtime {

void AllocationRouter::load_policy(const AllocationPolicy& policy) {
  auto p = std::make_shared<AllocationPolicy>(policy);
  std::lock_guard<std::mutex> lk(policy_mutex_);
  policy_ = std::move(p);
}

void AllocationRouter::clear_policy() noexcept {
  std::lock_guard<std::mutex> lk(policy_mutex_);
  policy_.reset();
}

bool AllocationRouter::has_policy() const noexcept {
  std::lock_guard<std::mutex> lk(policy_mutex_);
  return policy_ != nullptr;
}

RoutingDecision AllocationRouter::route(
    std::size_t size, std::size_t alignment,
    uint64_t site_hash, LifetimeHint hint) const noexcept
{
  RoutingDecision d;
  d.size_class_idx = SizeClassManager::class_index(size);
  d.rounded_size   = (d.size_class_idx < NUM_SIZE_CLASSES)
                       ? SIZE_CLASS_SIZES[d.size_class_idx]
                       : size;

  // Very large allocations: bypass arenas, use fallback.
  if (size > 32ULL * 1024 * 1024) {
    d.use_fallback = true;
    d.arena_id     = fallback_arena_id_;
    return d;
  }

  // Policy-based routing (highest priority).
  {
    std::lock_guard<std::mutex> lk(policy_mutex_);
    if (policy_) {
      auto opt = policy_->arena_for_site(site_hash);
      if (opt.has_value()) {
        d.arena_id = *opt;
        return d;
      }
    }
  }

  // Lifetime-hint routing.
  if (hint == LifetimeHint::LongLived) {
    d.arena_id = longlived_arena_id_;
    return d;
  }

  // Default: route by size class to default arena.
  d.arena_id = default_arena_id_;
  return d;
}

} // namespace runtime
} // namespace fragmentx

