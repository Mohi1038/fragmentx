#include <fragmentx/fragmentx.hpp>
#include <iostream>
#include <vector>

struct Message {
  uint64_t id;
  char payload[120];
};

int main() {
  std::cout << "FragmentX Basic Allocation Example\n";

  fragmentx::Config config;
  config.region_size = 16ULL * 1024 * 1024; // 16 MB regions
  fragmentx::Allocator allocator(config);

  // 1. Raw untyped allocation
  void* raw = allocator.allocate(256);
  std::cout << "Allocated 256 bytes at: " << raw << "\n";
  allocator.deallocate(raw);

  // 2. Typed allocation with lifetime hints
  std::vector<Message*> messages;
  for (int i = 0; i < 100; ++i) {
    auto* msg = allocator.construct<Message>(fragmentx::LifetimeHint::ShortLived);
    if (msg) {
      msg->id = static_cast<uint64_t>(i);
      messages.push_back(msg);
    }
  }

  std::cout << "Constructed " << messages.size() << " Message objects.\n";

  // Cleanup
  for (auto* m : messages) {
    allocator.destroy(m);
  }

  // 3. Inspect allocator statistics
  auto stats = allocator.statistics();
  std::cout << "\nAllocator Statistics:\n"
            << "  Total Allocations:   " << stats.total_allocations << "\n"
            << "  Total Deallocations: " << stats.total_deallocations << "\n"
            << "  Cache Hits:          " << stats.cache_hits << "\n"
            << "  Total Requested:     " << stats.requested_bytes << " bytes\n";

  return 0;
}

