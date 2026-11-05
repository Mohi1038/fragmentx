/// @file arena_manager.cpp
#include "arena_manager.hpp"
#include <cassert>
#include <cstring>

namespace fragmentx {
namespace runtime {

// ─── Arena::allocate ─────────────────────────────────────────────────────────

void* Arena::allocate(std::size_t size, std::size_t alignment) noexcept {
  if (!region || !active) return nullptr;

  const uint32_t class_idx = SizeClassManager::class_index(size);
  const std::size_t rounded = (class_idx < NUM_SIZE_CLASSES)
                                ? SIZE_CLASS_SIZES[class_idx]
                                : size;

  // Fast path: check per-arena free list.
  if (class_idx < NUM_SIZE_CLASSES) {
    if (void* recycled = free_lists.pop(class_idx)) {
      alloc_count.fetch_add(1, std::memory_order_relaxed);
      bytes_requested.fetch_add(size, std::memory_order_relaxed);
      return recycled;
    }
  }

  // Slow path: bump pointer from region.
  std::lock_guard<std::mutex> lk(mutex);
  void* ptr = region->bump_alloc(rounded, alignment ? alignment : 16);
  if (ptr) {
    alloc_count.fetch_add(1, std::memory_order_relaxed);
    bytes_requested.fetch_add(size, std::memory_order_relaxed);
  }
  return ptr;
}

void Arena::deallocate(void* ptr, uint32_t class_idx) noexcept {
  if (!ptr) return;
  if (class_idx < NUM_SIZE_CLASSES) {
    free_lists.push(class_idx, ptr);
    free_count.fetch_add(1, std::memory_order_relaxed);
  }
  // Large or unknown: leak within arena (region is reclaimed on destroy).
}

// ─── ArenaManager ────────────────────────────────────────────────────────────

ArenaManager::ArenaManager(RegionManager& regions, std::size_t default_region_size)
    : regions_(regions), default_region_size_(default_region_size) {}

uint32_t ArenaManager::create_arena(const ArenaConfig& config) {
  const std::size_t region_size =
      config.initial_size ? config.initial_size : default_region_size_;

  Region* reg = regions_.acquire(region_size,
      (config.type == ArenaType::LongLived) ? RegionType::Large : RegionType::Small);
  if (!reg) return UINT32_MAX;

  std::lock_guard<std::mutex> lk(mutex_);
  auto arena       = std::make_unique<Arena>();
  arena->id        = next_id_++;
  arena->type      = config.type;
  arena->region    = reg;
  arena->label     = std::string(config.label);

  const uint32_t id = arena->id;
  arenas_.push_back(std::move(arena));
  return id;
}

Arena* ArenaManager::get_arena(uint32_t id) noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  for (auto& a : arenas_) {
    if (a->id == id && a->active) return a.get();
  }
  return nullptr;
}

void ArenaManager::destroy_arena(uint32_t id) {
  std::lock_guard<std::mutex> lk(mutex_);
  for (auto it = arenas_.begin(); it != arenas_.end(); ++it) {
    if ((*it)->id == id) {
      regions_.release((*it)->region);
      (*it)->active = false;
      arenas_.erase(it);
      return;
    }
  }
}

uint32_t ArenaManager::count() const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return static_cast<uint32_t>(arenas_.size());
}

std::vector<ArenaDescriptor> ArenaManager::descriptors() const {
  std::lock_guard<std::mutex> lk(mutex_);
  std::vector<ArenaDescriptor> out;
  out.reserve(arenas_.size());
  for (auto& a : arenas_) {
    ArenaDescriptor d;
    d.id               = a->id;
    d.type             = a->type;
    d.reserved_bytes   = a->region ? a->region->size : 0;
    d.used_bytes       = a->region ? a->region->cursor : 0;
    d.total_allocations    = a->alloc_count.load(std::memory_order_relaxed);
    d.total_deallocations  = a->free_count.load(std::memory_order_relaxed);
    d.label            = a->label;
    out.push_back(d);
  }
  return out;
}

} // namespace runtime
} // namespace fragmentx

