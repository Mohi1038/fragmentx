#include <fragmentx/fragmentx.hpp>
#include <iostream>
#include <vector>

void process_request(fragmentx::Allocator& alloc, int req_id) {
  // Allocate phase-specific request buffers with LifetimeHint::ShortLived
  std::vector<void*> temp_buffers;
  for (int i = 0; i < 50; ++i) {
    void* buf = alloc.allocate(1024);
    temp_buffers.push_back(buf);
  }

  // Persist response summary with LifetimeHint::LongLived
  void* cache_entry = alloc.allocate(128);
  (void)cache_entry;

  // Cleanup request scope
  for (void* p : temp_buffers) {
    alloc.deallocate(p);
  }

  if (req_id % 100 == 0) {
    std::cout << "Processed request " << req_id << "\n";
  }
}

int main() {
  std::cout << "FragmentX Request/Phase Arena Allocation Example\n";

  fragmentx::Config cfg;
  fragmentx::Allocator allocator(cfg);

  for (int i = 1; i <= 500; ++i) {
    process_request(allocator, i);
  }

  auto stats = allocator.statistics();
  std::cout << "\nFinished workload:\n"
            << "  Total Allocations:   " << stats.total_allocations << "\n"
            << "  Total Deallocations: " << stats.total_deallocations << "\n"
            << "  Active Arenas:       " << stats.active_arenas << "\n";

  return 0;
}

