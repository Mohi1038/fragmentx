#pragma once
/// @file arena_manager.hpp
/// @brief Arena lifecycle management.

#include "region_manager.hpp"
#include "free_list.hpp"
#include "size_classes.hpp"
#include <fragmentx/arena.hpp>
#include <vector>
#include <mutex>
#include <memory>
#include <cstdint>

namespace fragmentx {
namespace runtime {

/// @brief Internal arena state.
struct Arena {
  uint32_t          id;
  ArenaType         type;
  Region*           region = nullptr;         ///< Backing OS region.
  SegregatedFreeList free_lists;              ///< Per-size-class free lists.
  std::mutex        mutex;                    ///< Guards bump pointer + free lists.

  // Statistics (updated under mutex or atomically).
  std::atomic<uint64_t> alloc_count{0};
  std::atomic<uint64_t> free_count{0};
  std::atomic<uint64_t> bytes_requested{0};

  std::string label;
  bool        active = true;

  Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  /// @brief Allocate from this arena.
  [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment) noexcept;

  /// @brief Return a block to this arena's free list.
  void deallocate(void* ptr, uint32_t class_idx) noexcept;
};

/// @brief Creates and manages Arena objects, backed by RegionManager.
class ArenaManager {
public:
  explicit ArenaManager(RegionManager& regions, std::size_t default_region_size);

  /// @brief Create a new arena with the given configuration.
  /// @return Arena ID on success, UINT32_MAX on failure.
  [[nodiscard]] uint32_t create_arena(const ArenaConfig& config);

  /// @brief Get a raw pointer to an arena by ID. Returns nullptr if not found.
  [[nodiscard]] Arena* get_arena(uint32_t id) noexcept;

  /// @brief Destroy an arena and release its backing region.
  void destroy_arena(uint32_t id);

  /// @return Number of active arenas.
  [[nodiscard]] uint32_t count() const noexcept;

  /// @brief Collect ArenaDescriptors for all active arenas.
  [[nodiscard]] std::vector<ArenaDescriptor> descriptors() const;

private:
  RegionManager&           regions_;
  std::size_t              default_region_size_;
  mutable std::mutex       mutex_;
  std::vector<std::unique_ptr<Arena>> arenas_;
  uint32_t                 next_id_ = 1;
};

} // namespace runtime
} // namespace fragmentx

