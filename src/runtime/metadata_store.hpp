#pragma once
/// @file metadata_store.hpp
/// @brief Per-allocation metadata kept separate from user data.

#include "size_classes.hpp"
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <vector>

namespace fragmentx {
namespace runtime {

/// @brief Record stored for every live allocation.
struct AllocationRecord {
  void*       ptr              = nullptr;
  std::size_t requested_size   = 0;
  std::size_t allocated_size   = 0;
  uint32_t    arena_id         = 0;
  uint32_t    size_class_index = 0;
  std::size_t alignment        = 0;
  uint64_t    alloc_time_ns    = 0;
};

/// @brief Thread-safe hash map from allocation address to AllocationRecord.
///
/// Uses open addressing with power-of-2 table size and linear probing.
/// Tombstone deletion for removed entries.
class MetadataStore {
public:
  explicit MetadataStore(std::size_t initial_capacity = 65536);
  ~MetadataStore();

  MetadataStore(const MetadataStore&)            = delete;
  MetadataStore& operator=(const MetadataStore&) = delete;

  /// @brief Insert or update a record. Thread-safe.
  void insert(void* ptr, const AllocationRecord& record);

  /// @brief Look up a record by pointer. Returns nullptr if not found.
  [[nodiscard]] const AllocationRecord* lookup(void* ptr) const;

  /// @brief Remove a record. Returns false if not found.
  bool remove(void* ptr);

  /// @return Number of live records in the store.
  [[nodiscard]] std::size_t size() const noexcept {
    return size_.load(std::memory_order_relaxed);
  }

private:
  struct Slot {
    enum class State : uint8_t { Empty, Occupied, Tombstone };
    std::atomic<State>  state{State::Empty};
    void*               key   = nullptr;
    AllocationRecord    value = {};

    Slot() noexcept = default;
    Slot(const Slot& other) noexcept
        : state(other.state.load(std::memory_order_relaxed)), key(other.key), value(other.value) {}
    Slot& operator=(const Slot& other) noexcept {
      key = other.key;
      value = other.value;
      state.store(other.state.load(std::memory_order_relaxed), std::memory_order_relaxed);
      return *this;
    }
    Slot(Slot&& other) noexcept : Slot(other) {}
    Slot& operator=(Slot&& other) noexcept { return *this = other; }
  };

  [[nodiscard]] std::size_t find_slot(void* ptr) const noexcept;
  void maybe_rehash();

  mutable std::mutex       mutex_;
  std::vector<Slot>        table_;
  std::atomic<std::size_t> size_{0};
};

} // namespace runtime
} // namespace fragmentx

