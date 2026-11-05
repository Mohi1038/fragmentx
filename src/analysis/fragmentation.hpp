#pragma once
/// @file fragmentation.hpp (internal analysis header)
#include <fragmentx/fragmentation.hpp>
#include <fragmentx/workload.hpp>
#include "trace_parser.hpp"

namespace fragmentx {
namespace analysis {
class FragmentationAnalyzer {
public:
  [[nodiscard]] static FragmentationAnalysisResult
  analyze(const WorkloadModel& model, const AllocationDatabase& db);
};
} // namespace analysis
} // namespace fragmentx

