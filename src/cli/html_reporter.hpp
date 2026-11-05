#pragma once

#include <fragmentx/workload.hpp>
#include <fragmentx/fragmentation.hpp>
#include <fragmentx/optimizer.hpp>
#include <string>

namespace fragmentx {
namespace cli {

class HtmlReporter {
public:
  static bool generate_report(
      const std::string& output_path,
      const WorkloadModel& model,
      const FragmentationAnalysisResult& frag,
      const OptimizationResult* opt_result = nullptr);
};

} // namespace cli
} // namespace fragmentx

