#pragma once
/// @file size_classes.hpp
/// @brief Size-class lookup and rounding logic.

#include <cstddef>
#include <cstdint>
#include <array>
#include <cassert>

namespace fragmentx {
namespace runtime {

/// @brief A single size class bucket.
struct SizeClass {
  std::size_t size;       ///< Canonical size for this class (all allocations rounded up).
  std::size_t alignment;  ///< Required alignment.
  uint32_t    index;      ///< Index into the size class table.
};

// Size classes: carefully chosen to minimise internal fragmentation
// while keeping the table small. Covers 1 byte through 4096 bytes in
// 20 classes; large allocations fall through to a separate path.
//
// Sizes (bytes): 8 16 24 32 48 64 96 128 192 256 384 512 768 1024 1536 2048 3072 4096
// Large: > 4096 → direct region allocation.

inline constexpr std::size_t NUM_SIZE_CLASSES = 18;
inline constexpr std::size_t LARGE_ALLOC_THRESHOLD = 4096;

inline constexpr std::array<std::size_t, NUM_SIZE_CLASSES> SIZE_CLASS_SIZES = {
     8,   16,   24,   32,   48,   64,   96,  128,
   192,  256,  384,  512,  768, 1024, 1536, 2048,
  3072, 4096
};

/// @brief Manage size-class lookup.
class SizeClassManager {
public:
  SizeClassManager() noexcept = default;

  /// @brief Return the size class index for the given requested size.
  ///        Returns NUM_SIZE_CLASSES for large allocations.
  [[nodiscard]] static uint32_t class_index(std::size_t requested) noexcept {
    for (uint32_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
      if (requested <= SIZE_CLASS_SIZES[i]) return i;
    }
    return NUM_SIZE_CLASSES; // large
  }

  /// @brief Round up a requested size to the nearest size class.
  [[nodiscard]] static std::size_t round_up(std::size_t requested) noexcept {
    uint32_t idx = class_index(requested);
    if (idx < NUM_SIZE_CLASSES) return SIZE_CLASS_SIZES[idx];
    return requested; // large: no rounding
  }

  /// @brief Return the alignment for a given size class index.
  [[nodiscard]] static std::size_t alignment_for(uint32_t class_idx) noexcept {
    if (class_idx >= NUM_SIZE_CLASSES) return 16;
    const std::size_t sz = SIZE_CLASS_SIZES[class_idx];
    // Objects ≥ 16 bytes get 16-byte alignment; smaller get natural alignment.
    return sz >= 16 ? 16 : (sz >= 8 ? 8 : 4);
  }

  /// @brief Compute internal fragmentation for an allocation.
  [[nodiscard]] static double internal_fragmentation(
      std::size_t requested, std::size_t size_class_size) noexcept {
    if (size_class_size == 0) return 0.0;
    return static_cast<double>(size_class_size - requested)
         / static_cast<double>(size_class_size);
  }

  /// @return true if @p size should be handled as a large allocation.
  [[nodiscard]] static bool is_large(std::size_t size) noexcept {
    return size > LARGE_ALLOC_THRESHOLD;
  }
};

} // namespace runtime
} // namespace fragmentx

