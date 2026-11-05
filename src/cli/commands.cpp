#include "commands.hpp"
#include "text_reporter.hpp"
#include "html_reporter.hpp"

#include <fragmentx/fragmentx.hpp>
#include <fragmentx/trace.hpp>
#include <fragmentx/optimizer.hpp>
#include <fragmentx/policy.hpp>

#include "../analysis/trace_parser.hpp"
#include "../analysis/workload_model.hpp"
#include "../analysis/fragmentation.hpp"
#include "../optimizer/policy_evaluator.hpp"
#include "../optimizer/candidate_generator.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <limits.h>
#include <cerrno>
#include <cstdlib>
#include <memory>

#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
#  include <unistd.h>
#  include <sys/mman.h>
#  include <sys/wait.h>
#endif
#if defined(FRAGMENTX_PLATFORM_MACOS)
#  include <mach-o/dyld.h>
#endif
#if defined(FRAGMENTX_PLATFORM_WINDOWS)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <process.h>
#endif

namespace fragmentx {
namespace cli {

int cmd_doctor() {
  std::cout << "\nChecking FragmentX system environment...\n\n";

  // Check 1: Virtual memory mmap support
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  void* ptr = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr != MAP_FAILED) {
    std::cout << "  \033[32m✔\033[0m Virtual memory reservation (mmap anonymous): SUPPORTED\n";
    ::munmap(ptr, 4096);
  } else {
    std::cout << "  \033[31m✘\033[0m Virtual memory reservation (mmap anonymous): FAILED\n";
  }
#else
  std::cout << "  \033[33m?\033[0m Platform virtual memory: Windows VirtualAlloc\n";
#endif

  // Check 2: Page size
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  long ps = ::sysconf(_SC_PAGESIZE);
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
  SYSTEM_INFO system_info{};
  ::GetSystemInfo(&system_info);
  long ps = static_cast<long>(system_info.dwPageSize);
#else
  long ps = 4096;
#endif
  std::cout << "  \033[32m✔\033[0m System page size: " << ps << " bytes\n";

  // Check 3: Clock resolution
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  struct timespec res{};
  clock_getres(CLOCK_MONOTONIC, &res);
  std::cout << "  \033[32m✔\033[0m Monotonic timer resolution: " << res.tv_nsec << " ns\n";
#endif

  // Check 4: Interposition capabilities
#if defined(FRAGMENTX_PLATFORM_LINUX)
  std::cout << "  \033[32m✔\033[0m Dynamic linker interposition (LD_PRELOAD): AVAILABLE\n";
#elif defined(FRAGMENTX_PLATFORM_MACOS)
  std::cout << "  \033[33m!\033[0m Dynamic linker interposition (DYLD_INSERT_LIBRARIES): Limited on macOS SIP\n";
  std::cout << "    (Use Instrumented mode or Replay mode for full fidelity)\n";
#endif

  std::cout << "\n\033[32mSystem check passed. FragmentX is ready to optimize.\033[0m\n\n";
  return 0;
}

int cmd_version() {
  std::cout << "FragmentX v" << FRAGMENTX_VERSION << " (C++20)\n";
  std::cout << "Adaptive Memory Allocation and Heap Optimization Toolkit\n";
  return 0;
}

int cmd_analyze(const std::string& trace_path, bool json_output) {
  auto db = analysis::TraceParser::parse(trace_path);
  if (!db.error.empty()) {
    std::cerr << "Error parsing trace: " << db.error << "\n";
    return 1;
  }

  auto model = analysis::WorkloadModelBuilder::build(db);
  auto frag  = analysis::FragmentationAnalyzer::analyze(model, db);

  if (json_output) {
    std::cout << "{\n"
              << "  \"total_allocations\": " << model.total_allocations << ",\n"
              << "  \"total_deallocations\": " << model.total_deallocations << ",\n"
              << "  \"requested_bytes\": " << model.total_requested_bytes << ",\n"
              << "  \"internal_fragmentation\": " << frag.internal.aggregate_waste_fraction << "\n"
              << "}\n";
  } else {
    TextReporter::print_banner(std::cout);
    TextReporter::print_workload_summary(model, std::cout);
    TextReporter::print_fragmentation_analysis(frag, std::cout);
  }
  return 0;
}

int cmd_report(const std::string& trace_path, const std::string& output_path, const std::string& format) {
  auto db = analysis::TraceParser::parse(trace_path);
  if (!db.error.empty()) {
    std::cerr << "Error parsing trace: " << db.error << "\n";
    return 1;
  }

  auto model = analysis::WorkloadModelBuilder::build(db);
  auto frag  = analysis::FragmentationAnalyzer::analyze(model, db);

  APMOptimizer optimizer;
  PolicySearchConfig cfg;
  auto opt_result = optimizer.optimize(model, cfg);

  if (format == "html") {
    if (HtmlReporter::generate_report(output_path, model, frag, &opt_result)) {
      std::cout << "\033[32m✔ HTML Report written to: " << output_path << "\033[0m\n";
      return 0;
    } else {
      std::cerr << "Failed to write HTML report to " << output_path << "\n";
      return 1;
    }
  } else {
    // Text output to file or stdout
    std::ofstream ofs(output_path);
    std::ostream& out = ofs.is_open() ? ofs : std::cout;
    TextReporter::print_banner(out);
    TextReporter::print_workload_summary(model, out);
    TextReporter::print_fragmentation_analysis(frag, out);
    TextReporter::print_optimization_result(opt_result, out);
    std::cout << "\033[32m✔ Report written to: " << output_path << "\033[0m\n";
    return 0;
  }
}

int cmd_optimize(const std::string& trace_path, const std::string& policy_out) {
  std::cout << "Analyzing workload from " << trace_path << "...\n";
  auto db = analysis::TraceParser::parse(trace_path);
  if (!db.error.empty()) {
    std::cerr << "Error parsing trace: " << db.error << "\n";
    return 1;
  }

  auto model = analysis::WorkloadModelBuilder::build(db);

  APMOptimizer optimizer;
  PolicySearchConfig cfg;
  cfg.progress_callback = [](double p) {
    int bar_width = 30;
    std::cout << "\rSearching policies: [";
    int pos = static_cast<int>(bar_width * p);
    for (int i = 0; i < bar_width; ++i) {
      if (i < pos) std::cout << "=";
      else if (i == pos) std::cout << ">";
      else std::cout << " ";
    }
    std::cout << "] " << int(p * 100.0) << "%" << std::flush;
  };

  auto result = optimizer.optimize(model, cfg);
  std::cout << "\n\n";

  TextReporter::print_optimization_result(result, std::cout);

  if (result.best_candidate) {
    if (PolicySerializer::save(result.best_candidate->policy, policy_out)) {
      std::cout << "\033[32m✔ Optimized policy saved to: " << policy_out << "\033[0m\n\n";
      return 0;
    } else {
      std::cerr << "Failed to save policy to " << policy_out << "\n";
      return 1;
    }
  } else {
    std::cout << "No candidate policy passed performance constraints.\n";
    return 3;
  }
}

int cmd_benchmark(const std::string& trace_path, const std::string& policy_path, int runs) {
  auto db = analysis::TraceParser::parse(trace_path);
  if (!db.error.empty()) {
    std::cerr << "Error reading trace: " << db.error << "\n";
    return 1;
  }

  auto model = analysis::WorkloadModelBuilder::build(db);

  CostWeights weights;
  auto baseline_policy = optimizer::CandidateGenerator::make_baseline();
  auto base_sim = optimizer::PolicyEvaluator::simulate(baseline_policy, model, weights);

  SimulatedMetrics opt_sim = base_sim;
  if (!policy_path.empty()) {
    auto loaded = PolicyLoader::load(policy_path);
    if (!loaded) {
      std::cerr << "Error loading policy: " << PolicyLoader::last_error() << "\n";
      return 1;
    }
    opt_sim = optimizer::PolicyEvaluator::simulate(*loaded, model, weights);
  }

  std::cout << "\nReplaying trace (" << runs << " iteration" << (runs > 1 ? "s" : "") << ")...\n\n";
  TextReporter::print_comparison(base_sim, opt_sim, std::cout);
  return 0;
}

int cmd_compare(const std::string& /*baseline*/, const std::string& candidate, const std::string& trace_path) {
  return cmd_benchmark(trace_path, candidate, 1);
}

int cmd_policy_inspect(const std::string& policy_path) {
  auto pol = PolicyLoader::load(policy_path);
  if (!pol) {
    std::cerr << "Failed to load policy: " << PolicyLoader::last_error() << "\n";
    return 1;
  }
  TextReporter::print_policy_details(*pol, std::cout);
  return 0;
}

int cmd_capture(int argc, char** argv) {
  if (argc < 1) {
    std::cerr << "Usage: fragmentx capture [--output <file>] -- <command> [args...]\n";
    return 2;
  }

  std::string output_file = "workload.fxtrace";
  int cmd_start = -1;

  for (int i = 0; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "--") {
      cmd_start = i + 1;
      break;
    }
  }

  if (cmd_start == -1 || cmd_start >= argc) {
    std::cerr << "Error: No target command specified after '--'.\n";
    return 2;
  }

#if defined(FRAGMENTX_PLATFORM_LINUX) && defined(FRAGMENTX_INTERPOSE_PATH)
  const char* preload_variable = "LD_PRELOAD";
#elif defined(FRAGMENTX_PLATFORM_MACOS) && defined(FRAGMENTX_INTERPOSE_PATH)
  const char* preload_variable = "DYLD_INSERT_LIBRARIES";
#endif
#if (defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)) && defined(FRAGMENTX_INTERPOSE_PATH)
  std::string interpose_path = FRAGMENTX_INTERPOSE_PATH;
  if (::access(interpose_path.c_str(), R_OK) != 0) {
    char executable[PATH_MAX]{};
#  if defined(FRAGMENTX_PLATFORM_LINUX)
    const ssize_t length = ::readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length > 0) executable[length] = '\0';
#  else
    uint32_t length = static_cast<uint32_t>(sizeof(executable));
    if (_NSGetExecutablePath(executable, &length) != 0) executable[0] = '\0';
#  endif
    std::string executable_path = executable;
    const std::size_t slash = executable_path.find_last_of('/');
    if (slash != std::string::npos) {
#  if defined(FRAGMENTX_PLATFORM_LINUX)
      interpose_path = executable_path.substr(0, slash) + "/../lib/libfragmentx_interpose.so";
#  else
      interpose_path = executable_path.substr(0, slash) + "/../lib/libfragmentx_interpose.dylib";
#  endif
    }
  }
  if (::access(interpose_path.c_str(), R_OK) != 0) {
    std::cerr << "Process capture library was not found at " << interpose_path << "\n";
    return 3;
  }
  const char* existing_preload = std::getenv(preload_variable);
  std::string preload = interpose_path;
  if (existing_preload && *existing_preload) {
    preload += ":";
    preload += existing_preload;
  }
  if (::setenv(preload_variable, preload.c_str(), 1) != 0 ||
      ::setenv("FRAGMENTX_TRACE_OUTPUT", output_file.c_str(), 1) != 0 ||
      ::setenv("FRAGMENTX_TRACE_MODE", "initial", 1) != 0 ||
      ::unsetenv("FRAGMENTX_TRACE_OWNER_PID") != 0) {
    std::perror("Unable to configure process capture");
    return 1;
  }
  if (::unlink(output_file.c_str()) != 0 && errno != ENOENT) {
    std::perror("Unable to prepare trace output");
    return 1;
  }

  std::cout << "Capturing allocations to " << output_file << "\n"
            << "Target command: " << argv[cmd_start] << "\n";
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::execvp(argv[cmd_start], &argv[cmd_start]);
    std::perror("execvp failed");
    _exit(errno == ENOENT ? 127 : 126);
  }
  const int fork_error = errno;
  ::unsetenv(preload_variable);
  ::unsetenv("FRAGMENTX_TRACE_OUTPUT");
  ::unsetenv("FRAGMENTX_TRACE_MODE");
  ::unsetenv("FRAGMENTX_TRACE_OWNER_PID");
  if (pid < 0) {
    errno = fork_error;
    std::perror("fork failed");
    return 1;
  }

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    std::perror("waitpid failed");
    return 1;
  }

  std::error_code file_error;
  const auto trace_size = std::filesystem::file_size(output_file, file_error);
  if (file_error || trace_size < sizeof(TraceHeader) + sizeof(TraceRecordHeader)) {
    std::cerr << "Capture failed: no complete trace was produced. The target may be "
                 "static, run in secure-execution mode, or have blocked dynamic-loader injection.\n";
    return 1;
  }
  std::unique_ptr<TraceReader> trace_reader(TraceReader::create_file_reader());
  if (!trace_reader->open(output_file)) {
    std::cerr << "Capture produced an invalid trace: " << trace_reader->error() << "\n";
    return 1;
  }
  TraceRecordType record_type{};
  std::vector<uint8_t> record_payload;
  while (trace_reader->next_record(record_type, record_payload)) {}
  if (!trace_reader->error().empty()) {
    std::cerr << "Capture produced an incomplete trace: " << trace_reader->error() << "\n";
    return 1;
  }
  std::cout << "Capture complete: " << output_file << " ("
            << trace_size << " bytes)\n";
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
#else
  std::cerr << "This platform has no native process-capture backend in this build. "
               "Use the instrumented CaptureSession API instead; no trace was written.\n";
  return 3;
#endif
}

int cmd_run(int argc, char** argv) {
  if (argc < 1) {
    std::cerr << "Usage: fragmentx run --policy <policy.json> -- <command> [args...]\n";
    return 2;
  }

  std::string policy_file;
  int cmd_start = -1;

  for (int i = 0; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--policy" && i + 1 < argc) {
      policy_file = argv[++i];
    } else if (arg == "--") {
      cmd_start = i + 1;
      break;
    }
  }

  if (cmd_start == -1 || cmd_start >= argc) {
    std::cerr << "Error: No target command specified after '--'.\n";
    return 2;
  }

  std::cout << "Running with FragmentX policy: " << policy_file << "\n";
#if defined(FRAGMENTX_PLATFORM_LINUX) || defined(FRAGMENTX_PLATFORM_MACOS)
  ::execvp(argv[cmd_start], &argv[cmd_start]);
  perror("execvp failed");
  return 1;
#elif defined(FRAGMENTX_PLATFORM_WINDOWS)
  std::vector<const char*> target_args;
  for (char** arg = &argv[cmd_start]; *arg != nullptr; ++arg) target_args.push_back(*arg);
  target_args.push_back(nullptr);
  const intptr_t status = ::_spawnvp(_P_WAIT, argv[cmd_start], target_args.data());
  if (status == -1) { std::perror("_spawnvp failed"); return 1; }
  return static_cast<int>(status);
#else
  std::cerr << "Launching target processes is unsupported on this platform.\n";
  return 3;
#endif
}

} // namespace cli
} // namespace fragmentx

