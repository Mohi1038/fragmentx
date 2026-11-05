#pragma once

#include <fragmentx/workload.hpp>
#include <string>

namespace fragmentx {
namespace optimizer {

struct ShiftReport {
  double shift_score = 0.0; // 0.0 (identical) to 1.0 (drastically different)
  bool trigger_reoptimize = false;
  std::string reason;
};

class WorkloadShiftDetector {
public:
  static ShiftReport detect_shift(
      const WorkloadModel& baseline,
      const WorkloadModel& current,
      double threshold = 0.25);
};

} // namespace optimizer
} // namespace fragmentx

