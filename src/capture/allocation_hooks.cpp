/// @file allocation_hooks.cpp
/// @brief Instrumented-mode allocation hooks.

#include <fragmentx/capture.hpp>
#include <fragmentx/trace.hpp>
#include "event_recorder.hpp"

#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>

#if defined(FRAGMENTX_PLATFORM_LINUX)
#  include <time.h>
#endif

namespace fragmentx {
namespace capture {

// Global recorder instance; set by CaptureSession::start().
static std::atomic<EventRecorder*> g_recorder{nullptr};
static std::atomic<uint64_t> g_active_hooks{0};
static std::atomic<uint64_t> g_event_id{0};
static thread_local bool t_in_hook = false;

static uint64_t hook_now_ns() noexcept {
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
       + static_cast<uint64_t>(ts.tv_nsec);
#else
  auto tp = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(tp).count());
#endif
}

bool set_global_recorder(EventRecorder* r) noexcept {
  if (r) {
    EventRecorder* expected = nullptr;
    return g_recorder.compare_exchange_strong(expected, r,
        std::memory_order_release, std::memory_order_relaxed);
  }

  g_recorder.exchange(nullptr, std::memory_order_acq_rel);
  while (g_active_hooks.load(std::memory_order_acquire) != 0)
    std::this_thread::yield();
  return true;
}

static EventRecorder* acquire_recorder() noexcept {
  while (true) {
    EventRecorder* recorder = g_recorder.load(std::memory_order_acquire);
    if (!recorder) return nullptr;
    g_active_hooks.fetch_add(1, std::memory_order_acq_rel);
    if (g_recorder.load(std::memory_order_acquire) == recorder) return recorder;
    g_active_hooks.fetch_sub(1, std::memory_order_release);
  }
}

static void release_recorder() noexcept {
  g_active_hooks.fetch_sub(1, std::memory_order_release);
}

void hook_alloc(void* ptr, std::size_t size, uint64_t site_hash,
                uint64_t thread_id, uint32_t arena_id) noexcept {
  if (t_in_hook || !ptr) return;
  EventRecorder* recorder = acquire_recorder();
  if (!recorder) return;
  t_in_hook = true;
  AllocPayload p{};
  p.event_id      = g_event_id.fetch_add(1, std::memory_order_relaxed);
  p.address       = reinterpret_cast<uint64_t>(ptr);
  p.requested_size= size;
  p.allocated_size= size;
  p.timestamp_ns  = hook_now_ns();
  p.site_hash     = site_hash;
  p.thread_id     = thread_id;
  p.arena_id      = arena_id;
  recorder->record_alloc(p);
  t_in_hook = false;
  release_recorder();
}

void hook_free(void* ptr, uint64_t thread_id) noexcept {
  if (t_in_hook || !ptr) return;
  EventRecorder* recorder = acquire_recorder();
  if (!recorder) return;
  t_in_hook = true;
  FreePayload p{};
  p.event_id    = g_event_id.fetch_add(1, std::memory_order_relaxed);
  p.address     = reinterpret_cast<uint64_t>(ptr);
  p.timestamp_ns= hook_now_ns();
  p.thread_id   = thread_id;
  recorder->record_free(p);
  t_in_hook = false;
  release_recorder();
}

} // namespace capture
} // namespace fragmentx

