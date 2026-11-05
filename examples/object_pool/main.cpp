#include <fragmentx/fragmentx.hpp>
#include <iostream>
#include <chrono>
#include <vector>

struct Packet {
  uint32_t seq;
  uint8_t  data[60];
};

int main() {
  std::cout << "FragmentX Object Pool Pattern Benchmark\n";

  fragmentx::Config config;
  fragmentx::Allocator allocator(config);

  const int ITERATIONS = 500000;
  std::vector<Packet*> pool;
  pool.reserve(1000);

  auto start = std::chrono::high_resolution_clock::now();

  for (int iter = 0; iter < ITERATIONS; ++iter) {
    if (pool.size() < 1000) {
      pool.push_back(allocator.allocate_typed<Packet>());
    } else {
      allocator.deallocate_typed(pool.back());
      pool.pop_back();
    }
  }

  for (auto* p : pool) {
    allocator.deallocate_typed(p);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  std::cout << "Completed " << ITERATIONS << " rapid pool operations in "
            << dur_us << " µs (" << (ITERATIONS * 1000000.0 / dur_us) << " ops/sec)\n";

  auto stats = allocator.statistics();
  std::cout << "Cache hit ratio: "
            << (stats.cache_hits * 100.0 / (stats.cache_hits + stats.cache_misses + 1))
            << "%\n";

  return 0;
}

