#include <fragmentx/fragmentx.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

int main() {
  std::cout << "FragmentX Workload Capture Example\n";

  fragmentx::Config cfg;
  cfg.enable_capture = true;
  cfg.capture_output_path = "workload_example.fxtrace";

  fragmentx::CaptureSession session(cfg);
  if (!session.start()) {
    std::cerr << "Failed to start capture session!\n";
    return 1;
  }
  std::cout << "Capture session started. Output: " << cfg.capture_output_path << "\n";

  // Simulate an application workload
  std::vector<void*> long_lived;
  for (int cycle = 0; cycle < 50; ++cycle) {
    // 100 short-lived objects
    std::vector<void*> short_lived;
    for (int i = 0; i < 100; ++i) {
      void* p = std::malloc(64 + (i % 128));
      short_lived.push_back(p);
    }

    // 2 long-lived objects
    long_lived.push_back(std::malloc(512));

    // Release short-lived objects
    for (void* p : short_lived) {
      std::free(p);
    }
  }

  // Cleanup long-lived
  for (void* p : long_lived) {
    std::free(p);
  }

  session.stop();
  auto stats = session.statistics();

  std::cout << "\nCapture finished:\n"
            << "  Events recorded: " << stats.alloc_events_recorded << "\n"
            << "  Events dropped:  " << stats.events_dropped << "\n"
            << "  Duration:        " << stats.capture_duration_ms << " ms\n"
            << "Trace written to " << cfg.capture_output_path << "\n";

  return 0;
}

