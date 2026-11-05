#pragma once
/// @file trace_parser.hpp

#include <fragmentx/capture.hpp>
#include <fragmentx/trace.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

namespace fragmentx {
namespace analysis {

struct AllocationDatabase {
  TraceHeader  header{};
  std::vector<AllocationEvent> events;
  std::unordered_map<uint64_t, std::size_t> live_at_end; // addr -> events index

  uint64_t total_allocs          = 0;
  uint64_t total_frees           = 0;
  uint64_t unmatched_allocs      = 0;
  uint64_t total_requested_bytes = 0;
  std::string error;
};

class TraceParser {
public:
  [[nodiscard]] static AllocationDatabase parse(
      const std::string& path,
      std::function<void(std::size_t events_processed,
                         std::size_t total_estimate)> progress = {});
};

} // namespace analysis
} // namespace fragmentx

