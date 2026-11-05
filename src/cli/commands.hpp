#pragma once

#include <vector>
#include <string>

namespace fragmentx {
namespace cli {

int cmd_doctor();
int cmd_version();
int cmd_analyze(const std::string& trace_path, bool json_output);
int cmd_report(const std::string& trace_path, const std::string& output_path, const std::string& format);
int cmd_optimize(const std::string& trace_path, const std::string& policy_out);
int cmd_benchmark(const std::string& trace_path, const std::string& policy_path, int runs);
int cmd_compare(const std::string& baseline, const std::string& candidate, const std::string& trace_path);
int cmd_policy_inspect(const std::string& policy_path);
int cmd_capture(int argc, char** argv);
int cmd_run(int argc, char** argv);

} // namespace cli
} // namespace fragmentx

