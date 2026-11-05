/// @file event_recorder.cpp
/// @brief Lock-free MPSC ring buffer for capturing allocation events.

#include "event_recorder.hpp"
#include <cstring>
#include <thread>
#include <chrono>

namespace fragmentx {
namespace capture {

EventRecorder::EventRecorder(uint32_t capacity, std::string output_path)
    : capacity_(capacity)
    , mask_(capacity - 1)
    , output_path_(std::move(output_path))
{
  // Capacity must be power of 2.
  assert((capacity & (capacity - 1)) == 0);
  buffer_.resize(capacity_);
  for (uint32_t i = 0; i < capacity_; ++i)
    buffer_[i].sequence.store(i, std::memory_order_relaxed);
  writer_ = std::unique_ptr<TraceWriter>(TraceWriter::create_file_writer());
}

bool EventRecorder::start() {
  if (!writer_->open(output_path_)) return false;

  running_.store(true, std::memory_order_release);
  drain_thread_ = std::thread([this]() { drain_loop(); });
  return true;
}

void EventRecorder::stop() {
  running_.store(false, std::memory_order_release);
  if (drain_thread_.joinable()) drain_thread_.join();
  writer_->flush();
  writer_->close();
}

void EventRecorder::record_alloc(const AllocPayload& payload) noexcept {
  if (!running_.load(std::memory_order_relaxed)) return;

  uint64_t seq = 0;
  if (!reserve_sequence(seq)) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t idx  = static_cast<uint32_t>(seq & mask_);
  Slot& slot          = buffer_[idx];

  // Spin until this slot is consumed (should be rare at typical rates).
  uint64_t expected = slot.sequence.load(std::memory_order_relaxed);
  if (expected != seq) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    slot.type = SlotType::Dropped;
    slot.sequence.store(seq + 1, std::memory_order_release);
    return;
  }

  slot.type = SlotType::Alloc;
  std::memcpy(&slot.alloc, &payload, sizeof(payload));
  slot.sequence.store(seq + 1, std::memory_order_release);
}

void EventRecorder::record_free(const FreePayload& payload) noexcept {
  if (!running_.load(std::memory_order_relaxed)) return;

  uint64_t seq = 0;
  if (!reserve_sequence(seq)) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint32_t idx = static_cast<uint32_t>(seq & mask_);
  Slot& slot         = buffer_[idx];

  if (slot.sequence.load(std::memory_order_relaxed) != seq) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    slot.type = SlotType::Dropped;
    slot.sequence.store(seq + 1, std::memory_order_release);
    return;
  }
  slot.type = SlotType::Free;
  std::memcpy(&slot.free, &payload, sizeof(payload));
  slot.sequence.store(seq + 1, std::memory_order_release);
}

bool EventRecorder::reserve_sequence(uint64_t& sequence) noexcept {
  uint64_t current = write_seq_.load(std::memory_order_relaxed);
  while (true) {
    const uint64_t consumed = read_seq_.load(std::memory_order_acquire);
    if (current - consumed >= capacity_) return false;
    if (write_seq_.compare_exchange_weak(current, current + 1,
          std::memory_order_relaxed, std::memory_order_relaxed)) {
      sequence = current;
      return true;
    }
  }
}

void EventRecorder::drain_loop() {
  while (running_.load(std::memory_order_acquire) || has_pending()) {
    drain_once();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  drain_once(); // Final flush.
}

bool EventRecorder::has_pending() const noexcept {
  return read_seq_.load(std::memory_order_relaxed)
       < write_seq_.load(std::memory_order_relaxed);
}

void EventRecorder::drain_once() {
  while (true) {
    const uint64_t rseq = read_seq_.load(std::memory_order_relaxed);
    const uint32_t idx  = static_cast<uint32_t>(rseq & mask_);
    Slot& slot          = buffer_[idx];

    if (slot.sequence.load(std::memory_order_acquire) != rseq + 1) break;

    if (slot.type == SlotType::Alloc)
      writer_->write_alloc(slot.alloc);
    else if (slot.type == SlotType::Free)
      writer_->write_free(slot.free);

    slot.sequence.store(rseq + capacity_, std::memory_order_release);
    read_seq_.store(rseq + 1, std::memory_order_release);
    recorded_.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace capture
} // namespace fragmentx

