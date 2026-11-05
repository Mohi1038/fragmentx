/// @file capture_session.cpp
/// @brief CaptureSession implementation.

#include <fragmentx/capture.hpp>
#include <fragmentx/config.hpp>
#include "event_recorder.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <cstdio>

namespace fragmentx {

struct CaptureSession::Impl {
  Config config;
  std::unique_ptr<capture::EventRecorder> recorder;
  std::atomic<bool> active{false};
  CaptureStatistics stats;
  std::chrono::steady_clock::time_point start_time;
};

CaptureSession::CaptureSession(const Config& config) {
  impl_ = new Impl{};
  impl_->config = config;
}

CaptureSession::~CaptureSession() {
  if (impl_->active.load()) stop();
  delete impl_;
}

bool CaptureSession::start() {
  if (impl_->active.load()) return false;

  impl_->recorder = std::make_unique<capture::EventRecorder>(
      impl_->config.capture_ring_buffer_capacity,
      impl_->config.capture_output_path);

  if (!capture::set_global_recorder(impl_->recorder.get())) {
    return false;
  }
  if (!impl_->recorder->start()) {
    (void)capture::set_global_recorder(nullptr);
    return false;
  }

  impl_->start_time = std::chrono::steady_clock::now();
  impl_->active.store(true, std::memory_order_release);
  return true;
}

void CaptureSession::stop() {
  if (!impl_->active.load()) return;
  impl_->active.store(false, std::memory_order_release);

  (void)capture::set_global_recorder(nullptr);

  if (impl_->recorder) {
    impl_->recorder->stop();
    auto end = std::chrono::steady_clock::now();
    impl_->stats.alloc_events_recorded  = impl_->recorder->recorded_count();
    impl_->stats.events_dropped         = impl_->recorder->dropped_count();
    impl_->stats.capture_duration_ms    =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            end - impl_->start_time).count());
  }
}

bool CaptureSession::is_active() const noexcept {
  return impl_->active.load(std::memory_order_acquire);
}

CaptureStatistics CaptureSession::statistics() const noexcept {
  if (impl_->recorder) {
    impl_->stats.alloc_events_recorded = impl_->recorder->recorded_count();
    impl_->stats.events_dropped        = impl_->recorder->dropped_count();
  }
  return impl_->stats;
}

double CaptureSession::overhead_estimate_ns() const noexcept {
  // Empirical estimate: ~25ns per event on a modern CPU (ring buffer write).
  return 25.0;
}

void CaptureSession::on_buffer_pressure(double /*threshold*/,
                                        std::function<void()> /*callback*/) {
  // TODO: implement pressure callback in EventRecorder.
}

} // namespace fragmentx

