#include <benchmark/benchmark.h>

#include <fragmentx/allocator.hpp>

#include <cstdlib>

namespace {

void benchmark_system_allocator(benchmark::State& state) {
  const auto size = static_cast<std::size_t>(state.range(0));
  for (auto _ : state) {
    void* memory = std::malloc(size);
    benchmark::DoNotOptimize(memory);
    benchmark::ClobberMemory();
    std::free(memory);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(size));
}

void benchmark_fragmentx_allocator(benchmark::State& state) {
  fragmentx::Allocator allocator;
  const auto size = static_cast<std::size_t>(state.range(0));
  for (auto _ : state) {
    void* memory = allocator.allocate(size);
    benchmark::DoNotOptimize(memory);
    benchmark::ClobberMemory();
    allocator.deallocate(memory);
  }
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(size));
}

BENCHMARK(benchmark_system_allocator)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(benchmark_fragmentx_allocator)->RangeMultiplier(4)->Range(16, 4096);

} // namespace

BENCHMARK_MAIN();
