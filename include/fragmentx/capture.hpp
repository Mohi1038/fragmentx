#pragma once

/// @file capture.hpp
/// @brief Allocation event capture and CaptureSession API.

#include <fragmentx/statistics.hpp>
#include <fragmentx/config.hpp>
#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

namespace fragmentx {

// ─── AllocationEventType ────────────────────────────────────────────────────

/// @brief Type of a recorded allocation event.
enum class AllocationEventType : uint8_t {
  Allocate   = 0x01, ///< malloc / operator new / fxalloc
  Deallocate = 0x02, ///< free  / operator delete / fxfree
  Reallocate = 0x03, ///< realloc / fxrealloc
};

// ─── AllocationEvent ────────────────────────────────────────────────────────

/// @brief A single allocation or deallocation event recorded during capture.
///
/// For paired ALLOC/FREE events, the analyzer matches them by address and
/// computes: lifetime_ns = free_timestamp_ns - alloc_timestamp_ns.
struct AllocationEvent {
  /// Backend-defined event identifier. Trace record order is authoritative
  /// when combining events from multiple processes.
  uint64_t event_id = 0;

  /// Virtual address returned by the allocator (for ALLOC) or passed to free
  /// (for FREE/REALLOC).
  uint64_t address = 0;

  /// For ALLOC/REALLOC: bytes requested by the application.
  uint64_t requested_size = 0;

  /// For ALLOC/REALLOC: bytes actually reserved by the allocator.
  /// May be larger than requested_size due to size-class rounding.
  /// 0 if the allocator did not expose this information.
  uint64_t allocated_size = 0;

  /// Timestamp in nanoseconds (CLOCK_MONOTONIC_RAW on Linux).
  uint64_t timestamp_ns = 0;

  /// For FREE events on paired allocations: timestamp when the allocation
  /// was freed. 0 until the analyzer matches ALLOC+FREE pairs.
  uint64_t free_timestamp_ns = 0;

  /// FNV-1a hash of the call site (return address at the allocation call).
  uint64_t site_hash = 0;

  /// Thread identifier. Process interposition stores the process ID in the
  /// upper 32 bits and the native thread ID in the lower 32 bits.
  uint64_t thread_id = 0;

  /// Event type.
  AllocationEventType type = AllocationEventType::Allocate;

  /// Arena ID assigned by FragmentX's allocator (0 if unknown or fallback).
  uint32_t arena_id = 0;

  // ── Computed fields (set by the analyzer) ─────────────────────────────

  /// Object lifetime in nanoseconds. Set by the analyzer after matching
  /// ALLOC+FREE pairs. UINT64_MAX for objects still live at trace end.
  uint64_t lifetime_ns = UINT64_MAX;

  /// @return true if this allocation was freed before the trace ended.
  [[nodiscard]] bool is_freed() const noexcept {
    return lifetime_ns != UINT64_MAX;
  }

  /// @return Lifetime in nanoseconds, or 0 if not yet freed.
  [[nodiscard]] uint64_t lifetime_or_zero() const noexcept {
    return is_freed() ? lifetime_ns : 0ULL;
  }
};

// ─── CaptureSession ─────────────────────────────────────────────────────────

/// @brief High-level capture session manager.
///
/// Start a session before the workload begins and stop it afterwards.
/// The session writes a binary .fxtrace file that can be analyzed offline.
///
/// @code
/// fragmentx::Config cfg;
/// cfg.enable_capture        = true;
/// cfg.capture_output_path   = "workload.fxtrace";
/// fragmentx::CaptureSession session(cfg);
/// session.start();
/// fragmentx::capture::hook_alloc(ptr, size, site_hash, thread_id);
/// run_my_workload();
/// fragmentx::capture::hook_free(ptr, thread_id);
/// session.stop();
/// auto stats = session.statistics();
/// @endcode
class CaptureSession {
public:
  explicit CaptureSession(const Config& config);
  ~CaptureSession();

  // Non-copyable, non-movable (singleton resource).
  CaptureSession(const CaptureSession&)            = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;
  CaptureSession(CaptureSession&&)                 = delete;
  CaptureSession& operator=(CaptureSession&&)      = delete;

  /// @brief Begin recording allocation events.
  /// @return true if the session started successfully.
  [[nodiscard]] bool start();

  /// @brief Stop recording, flush all pending events, and close the trace file.
  void stop();

  /// @return true if the session is currently recording.
  [[nodiscard]] bool is_active() const noexcept;

  /// @return Snapshot of capture statistics. Thread-safe.
  [[nodiscard]] CaptureStatistics statistics() const noexcept;

  /// @return Estimated per-allocation capture overhead in nanoseconds.
  ///         Measured as the median cost of a no-op allocation hook call.
  [[nodiscard]] double overhead_estimate_ns() const noexcept;

  /// @brief Set a callback invoked on the writer thread when the ring buffer
  ///        reaches the given fill fraction (0.0–1.0).
  void on_buffer_pressure(double threshold, std::function<void()> callback);

private:
  struct Impl;
  Impl* impl_ = nullptr;
};

namespace capture {

/// Record an allocation from source-instrumented code during an active session.
void hook_alloc(void* ptr, std::size_t size, uint64_t site_hash,
                uint64_t thread_id, uint32_t arena_id = 0) noexcept;

/// Record a deallocation from source-instrumented code during an active session.
void hook_free(void* ptr, uint64_t thread_id) noexcept;

} // namespace capture

// ─── Capture-site helpers ────────────────────────────────────────────────────

/// @brief Compute an FNV-1a site hash from a return address.
/// @param return_address  Value of __builtin_return_address(0) at the call site.
[[nodiscard]] inline uint64_t site_hash_from_address(const void* return_address) noexcept {
  // FNV-1a 64-bit hash of the pointer value.
  uint64_t hash = 14695981039346656037ULL;
  auto val = reinterpret_cast<uintptr_t>(return_address);
  for (std::size_t i = 0; i < sizeof(val); ++i) {
    hash ^= static_cast<uint8_t>(val & 0xFF);
    hash *= 1099511628211ULL;
    val >>= 8;
  }
  return hash;
}

} // namespace fragmentx

