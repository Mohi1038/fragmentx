#pragma once
/// @file event_recorder.hpp
/// @brief MPSC lock-free ring buffer for capture events.

#include <fragmentx/trace.hpp>
#include <fragmentx/capture.hpp>
#include <atomic>
#include <vector>
#include <thread>
#include <memory>
#include <string>
#include <cassert>

namespace fragmentx {
namespace capture {

enum class SlotType : uint8_t { Alloc, Free, Dropped };

struct Slot {
  std::atomic<uint64_t> sequence{0};
  SlotType type = SlotType::Alloc;
  union {
    AllocPayload alloc;
    FreePayload  free;
  };

  Slot() noexcept : alloc{} {}
  Slot(const Slot& other) noexcept : sequence(other.sequence.load(std::memory_order_relaxed)), type(other.type) {
    if (type == SlotType::Alloc) alloc = other.alloc;
    else free = other.free;
  }
  Slot& operator=(const Slot& other) noexcept {
    type = other.type;
    if (type == SlotType::Alloc) alloc = other.alloc;
    else free = other.free;
    sequence.store(other.sequence.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }
  Slot(Slot&& other) noexcept : Slot(other) {}
  Slot& operator=(Slot&& other) noexcept { return *this = other; }
};

/// @brief Multi-producer single-consumer ring buffer for allocation events.
///        Producers (allocation threads) write; a background drain thread reads.
class EventRecorder {
public:
  EventRecorder(uint32_t capacity, std::string output_path);

  [[nodiscard]] bool start();
  void stop();

  void record_alloc(const AllocPayload& payload) noexcept;
  void record_free(const FreePayload&  payload) noexcept;

  [[nodiscard]] uint64_t recorded_count() const noexcept {
    return recorded_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] uint64_t dropped_count() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

private:
  void drain_loop();
  void drain_once();
  [[nodiscard]] bool has_pending() const noexcept;
  [[nodiscard]] bool reserve_sequence(uint64_t& sequence) noexcept;

  const uint32_t  capacity_;
  const uint32_t  mask_;
  std::string     output_path_;

  alignas(64) std::atomic<uint64_t> write_seq_{0};
  alignas(64) std::atomic<uint64_t> read_seq_{0};

  std::vector<Slot>              buffer_;
  std::unique_ptr<TraceWriter>   writer_;
  std::thread                    drain_thread_;
  std::atomic<bool>              running_{false};
  std::atomic<uint64_t>          recorded_{0};
  std::atomic<uint64_t>          dropped_{0};
};

/// Install or remove the process-wide destination used by source-instrumented
/// allocation hooks. Only one CaptureSession can record in a process at once.
[[nodiscard]] bool set_global_recorder(EventRecorder* recorder) noexcept;

} // namespace capture
} // namespace fragmentx

