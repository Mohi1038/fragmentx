#pragma once
/// @file region_manager.hpp
/// @brief OS-level memory region management.

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <algorithm>

namespace fragmentx {
namespace runtime {

/// @brief Type of a memory region, controlling how the allocator uses it.
enum class RegionType : uint8_t {
  Small,        ///< Used for small object arenas (≤ 256 bytes).
  Medium,       ///< Medium objects (257 – 4096 bytes).
  Large,        ///< Large objects (> 4096 bytes).
  Metadata,     ///< Allocator-internal metadata (never user data).
};

/// @brief A contiguous range of virtual address space reserved from the OS.
struct Region {
  std::byte*   base      = nullptr; ///< Base virtual address.
  std::size_t  size      = 0;       ///< Total reserved bytes.
  std::size_t  committed = 0;       ///< Bytes backed by physical pages.
  std::size_t  cursor    = 0;       ///< Bump pointer offset (bytes from base).
  RegionType   type      = RegionType::Small;
  bool         in_use    = false;

  /// @return Remaining bump-pointer capacity.
  [[nodiscard]] std::size_t remaining() const noexcept {
    return size > cursor ? size - cursor : 0;
  }

  /// @return Bump-allocate @p bytes from this region.
  ///         Returns nullptr if capacity is insufficient.
  [[nodiscard]] void* bump_alloc(std::size_t bytes, std::size_t alignment) noexcept {
    // Align cursor.
    const std::size_t aligned_cursor =
        (cursor + alignment - 1) & ~(alignment - 1);
    if (aligned_cursor + bytes > size) return nullptr;
    void* ptr = base + aligned_cursor;
    cursor    = aligned_cursor + bytes;
    return ptr;
  }
};

/// @brief Aggregate OS-level memory statistics.
struct RegionStats {
  std::size_t total_reserved  = 0;
  std::size_t total_committed = 0;
  std::size_t total_released  = 0;
  uint64_t    regions_acquired = 0;
  uint64_t    regions_released = 0;
};

/// @brief Manages acquisition and release of OS memory regions.
class RegionManager {
public:
  ~RegionManager();

  /// @return The OS page size in bytes (cached after first call).
  static std::size_t page_size() noexcept;

  /// @brief Acquire a new region of at least @p min_size bytes.
  /// @return Pointer to Region, or nullptr on failure.
  [[nodiscard]] Region* acquire(std::size_t min_size,
                                RegionType type = RegionType::Small);

  /// @brief Return a region to the OS.
  void release(Region* region);

  /// @brief Advise the OS that pages beyond the bump cursor are unused.
  void decommit_unused(Region* region);

  /// @return Aggregate statistics.
  [[nodiscard]] RegionStats statistics() const noexcept;

private:
  mutable std::mutex     mutex_;
  std::vector<Region*>   regions_;
  RegionStats            stats_;
};

} // namespace runtime
} // namespace fragmentx

