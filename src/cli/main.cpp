/// @file main.cpp
/// @brief FragmentX command-line interface.

#include "commands.hpp"
#include "text_reporter.hpp"
#include <fragmentx/fragmentx.hpp>

#include <iostream>
#include <string>
#include <vector>

static void print_usage() {
  fragmentx::cli::TextReporter::print_banner(std::cout);
  std::cout << R"(Usage: fragmentx <command> [options]

Commands:
  doctor                                Check environment and system capabilities
  capture  --output <file> -- <cmd>     Capture allocations (Linux and macOS backends)
  analyze  <trace.fxtrace> [--json]     Analyze workload patterns and fragmentation
  report   <trace.fxtrace> [options]    Generate an HTML or text optimization report
  optimize <trace.fxtrace> [options]    Run APM optimizer to synthesize an allocation policy
  benchmark <trace.fxtrace> [options]   Replay workload trace to evaluate policy performance
  compare  --baseline <b> --candidate <c> <trace>  Side-by-side comparison of two policies
  run      --policy <policy.json> -- <cmd>         Launch program with FragmentX allocator
  policy inspect <policy.json>          Inspect an existing policy's arena assignments
  version                               Print version information
  help                                  Show this help message

Options:
  --output, -o <file>   Output destination file
  --format <html|text>  Report format (default: html)
  --json                Emit machine-readable JSON output
  --runs <N>            Number of benchmark replay runs (default: 1)
  --help, -h            Show help for a command

Run 'fragmentx <command> --help' for command-specific options.
)";
}

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  std::string cmd = argv[1];

  if (cmd == "--help" || cmd == "-h" || cmd == "help") {
    print_usage();
    return 0;
  }

  if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    return fragmentx::cli::cmd_version();
  }

  if (cmd == "doctor") {
    return fragmentx::cli::cmd_doctor();
  }

  if (cmd == "analyze") {
    if (argc < 3) {
      std::cerr << "Usage: fragmentx analyze <trace.fxtrace> [--json]\n";
      return 2;
    }
    std::string trace_file = argv[2];
    bool json_mode = (argc >= 4 && std::string(argv[3]) == "--json");
    return fragmentx::cli::cmd_analyze(trace_file, json_mode);
  }

  if (cmd == "report") {
    if (argc < 3) {
      std::cerr << "Usage: fragmentx report <trace.fxtrace> [--output <report.html>] [--format html|text]\n";
      return 2;
    }
    std::string trace_file = argv[2];
    std::string out_file = "report.html";
    std::string format = "html";

    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
        out_file = argv[++i];
      } else if (arg == "--format" && i + 1 < argc) {
        format = argv[++i];
      }
    }
    return fragmentx::cli::cmd_report(trace_file, out_file, format);
  }

  if (cmd == "optimize") {
    if (argc < 3) {
      std::cerr << "Usage: fragmentx optimize <trace.fxtrace> [--output <policy.json>]\n";
      return 2;
    }
    std::string trace_file = argv[2];
    std::string policy_out = "optimized-policy.json";

    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
        policy_out = argv[++i];
      }
    }
    return fragmentx::cli::cmd_optimize(trace_file, policy_out);
  }

  if (cmd == "benchmark") {
    if (argc < 3) {
      std::cerr << "Usage: fragmentx benchmark <trace.fxtrace> [--policy <policy.json>] [--runs N]\n";
      return 2;
    }
    std::string trace_file = argv[2];
    std::string policy_file;
    int runs = 1;

    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--policy" && i + 1 < argc) {
        policy_file = argv[++i];
      } else if (arg == "--runs" && i + 1 < argc) {
        runs = std::stoi(argv[++i]);
      }
    }
    return fragmentx::cli::cmd_benchmark(trace_file, policy_file, runs);
  }

  if (cmd == "compare") {
    std::string baseline = "system-malloc";
    std::string candidate;
    std::string trace_file;

    for (int i = 2; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--baseline" && i + 1 < argc) {
        baseline = argv[++i];
      } else if (arg == "--candidate" && i + 1 < argc) {
        candidate = argv[++i];
      } else if (arg[0] != '-') {
        trace_file = arg;
      }
    }

    if (trace_file.empty() || candidate.empty()) {
      std::cerr << "Usage: fragmentx compare --baseline <policy> --candidate <policy.json> <trace.fxtrace>\n";
      return 2;
    }
    return fragmentx::cli::cmd_compare(baseline, candidate, trace_file);
  }

  if (cmd == "policy") {
    if (argc >= 4 && std::string(argv[2]) == "inspect") {
      return fragmentx::cli::cmd_policy_inspect(argv[3]);
    }
    std::cerr << "Usage: fragmentx policy inspect <policy.json>\n";
    return 2;
  }

  if (cmd == "capture") {
    return fragmentx::cli::cmd_capture(argc - 2, argv + 2);
  }

  if (cmd == "run") {
    return fragmentx::cli::cmd_run(argc - 2, argv + 2);
  }

  std::cerr << "Unknown command: " << cmd << "\n";
  print_usage();
  return 2;
}

