#pragma once
/// @file free_list.hpp
/// @brief Intrusive per-size-class free lists.
///
/// FreeList stores freed blocks using the first sizeof(void*) bytes OF the
/// freed block itself as a next-pointer. This is safe because:
///   1. The block is not in use by the application.
///   2. The allocator owns the memory.
///   3. The block is at least 8 bytes (minimum size class).
///
/// The GlobalFreeList uses a lock for thread safety.
/// ThreadLocalFreeList is per-thread and lock-free.

#include "size_classes.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <array>

namespace fragmentx {
namespace runtime {

// ─── Intrusive node ──────────────────────────────────────────────────────────

/// @brief Intrusive linked-list node stored in freed memory.
struct FreeNode {
  FreeNode* next = nullptr;
};

// ─── FreeList (mutex-protected) ──────────────────────────────────────────────

/// @brief A simple LIFO free list protected by a mutex.
class FreeList {
public:
  /// @brief Push a freed block onto the list.
  void push(void* ptr) noexcept {
    if (!ptr) return;
    auto* node  = static_cast<FreeNode*>(ptr);
    std::lock_guard<std::mutex> lk(mutex_);
    node->next  = head_;
    head_       = node;
    ++count_;
  }

  /// @brief Pop a block from the list. Returns nullptr if empty.
  [[nodiscard]] void* pop() noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!head_) return nullptr;
    FreeNode* node = head_;
    head_          = node->next;
    node->next     = nullptr;
    --count_;
    return node;
  }

  [[nodiscard]] bool empty() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return head_ == nullptr;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return count_;
  }

private:
  mutable std::mutex mutex_;
  FreeNode*          head_  = nullptr;
  std::size_t        count_ = 0;
};

// ─── ThreadLocalFreeList ─────────────────────────────────────────────────────

/// @brief Per-thread, per-size-class free list (no locking).
/// Holds up to @p Capacity blocks; excess are flushed to the global list.
template <std::size_t Capacity = 64>
class ThreadLocalFreeList {
public:
  void push(void* ptr) noexcept {
    if (count_ < Capacity) {
      slots_[count_++] = ptr;
    }
    // Excess: caller is responsible for flushing to global list.
  }

  [[nodiscard]] void* pop() noexcept {
    if (count_ == 0) return nullptr;
    return slots_[--count_];
  }

  [[nodiscard]] bool full()  const noexcept { return count_ == Capacity; }
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  /// Drain all cached blocks, calling @p fn on each.
  template <typename Fn>
  void flush(Fn&& fn) noexcept {
    for (std::size_t i = 0; i < count_; ++i) fn(slots_[i]);
    count_ = 0;
  }

private:
  std::array<void*, Capacity> slots_ = {};
  std::size_t                 count_ = 0;
};

// ─── SegregatedFreeList ───────────────────────────────────────────────────────

/// @brief One global FreeList per size class.
class SegregatedFreeList {
public:
  void push(uint32_t class_idx, void* ptr) noexcept {
    if (class_idx < NUM_SIZE_CLASSES) lists_[class_idx].push(ptr);
  }

  [[nodiscard]] void* pop(uint32_t class_idx) noexcept {
    if (class_idx < NUM_SIZE_CLASSES) return lists_[class_idx].pop();
    return nullptr;
  }

  [[nodiscard]] std::size_t size(uint32_t class_idx) const noexcept {
    if (class_idx < NUM_SIZE_CLASSES) return lists_[class_idx].size();
    return 0;
  }

private:
  std::array<FreeList, NUM_SIZE_CLASSES> lists_;
};

} // namespace runtime
} // namespace fragmentx

