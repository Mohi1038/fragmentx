#pragma once
/// @file workload_model.hpp
#include <fragmentx/workload.hpp>
#include "trace_parser.hpp"

namespace fragmentx {
namespace analysis {

class WorkloadModelBuilder {
public:
  [[nodiscard]] static WorkloadModel build(const AllocationDatabase& db);
};

} // namespace analysis
} // namespace fragmentx

