/// @file config.cpp
/// @brief Configuration validation and measurement methodology documentation.

#include <fragmentx/config.hpp>
#include <fragmentx/statistics.hpp>

namespace fragmentx {

std::string Config::validate() const {
  if (region_size == 0) {
    return "region_size must be greater than 0";
  }
  if (region_size % 4096 != 0) {
    return "region_size must be a multiple of system page size (4096)";
  }
  if (max_arenas == 0) {
    return "max_arenas must be at least 1";
  }
  if (default_alignment == 0 || (default_alignment & (default_alignment - 1)) != 0) {
    return "default_alignment must be a power of two";
  }
  if (capture_ring_buffer_capacity == 0 || (capture_ring_buffer_capacity & (capture_ring_buffer_capacity - 1)) != 0) {
    return "capture_ring_buffer_capacity must be a power of two";
  }
  return "";
}

const char* FragmentationReport::measurement_methodology() noexcept {
  return "FragmentX Measurement Methodology:\n"
         "- Internal fragmentation: (reserved_live - requested_live) / reserved_live, calculated per size class.\n"
         "- External fragmentation: estimated through contiguous free block availability modeling.\n"
         "- Retention: ratio of retained pages beyond active bump/free cursors.\n"
         "- Latencies: cycle-accurate timer percentiles (p50, p95, p99) under synthetic/replayed workloads.\n"
         "- Transparent reporting: estimates are explicitly separated from OS-level hardware counters.\n";
}

} // namespace fragmentx

