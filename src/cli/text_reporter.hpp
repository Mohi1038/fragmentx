#pragma once

#include <fragmentx/workload.hpp>
#include <fragmentx/fragmentation.hpp>
#include <fragmentx/optimizer.hpp>
#include <string>
#include <ostream>

namespace fragmentx {
namespace cli {

class TextReporter {
public:
  static void print_banner(std::ostream& os);
  static void print_workload_summary(const WorkloadModel& model, std::ostream& os);
  static void print_fragmentation_analysis(const FragmentationAnalysisResult& frag, std::ostream& os);
  static void print_optimization_result(const OptimizationResult& result, std::ostream& os);
  static void print_policy_details(const AllocationPolicy& policy, std::ostream& os);
  static void print_comparison(const SimulatedMetrics& base, const SimulatedMetrics& opt, std::ostream& os);
};

} // namespace cli
} // namespace fragmentx

