#pragma once
/// @file thread_cache.hpp
/// @brief Per-thread allocation cache to reduce global arena contention.

#include "free_list.hpp"
#include "size_classes.hpp"
#include <array>
#include <cstdint>

namespace fragmentx {
namespace runtime {

/// @brief Per-thread free-list cache for small objects.
///
/// Each thread holds its own array of free blocks (one list per size class).
/// When an object is freed, it goes into the thread-local cache first.
/// When the cache is full, excess blocks are flushed to the global
/// SegregatedFreeList. This amortizes the cost of mutex acquisition.
class ThreadCache {
public:
  static constexpr std::size_t MAX_CACHED_PER_CLASS = 64;

  struct PerClass {
    std::array<void*, MAX_CACHED_PER_CLASS> slots = {};
    std::size_t count = 0;

    void push(void* ptr) noexcept {
      if (count < MAX_CACHED_PER_CLASS) slots[count++] = ptr;
    }
    [[nodiscard]] void* pop() noexcept {
      return count > 0 ? slots[--count] : nullptr;
    }
    [[nodiscard]] bool full()  const noexcept { return count == MAX_CACHED_PER_CLASS; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
  };

  std::array<PerClass, NUM_SIZE_CLASSES> per_class;

  /// @brief Total blocks currently cached across all classes.
  [[nodiscard]] std::size_t total_cached() const noexcept {
    std::size_t n = 0;
    for (auto& pc : per_class) n += pc.count;
    return n;
  }
};

} // namespace runtime
} // namespace fragmentx

