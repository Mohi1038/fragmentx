# Allocator microbenchmarks

Build the benchmark executable with:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DFRAGMENTX_BUILD_BENCHMARKS=ON \
  -DFRAGMENTX_BUILD_TESTS=OFF \
  -DFRAGMENTX_BUILD_EXAMPLES=OFF
cmake --build build-bench --parallel
```

Run a short smoke comparison with:

```sh
./build-bench/benchmarks/fragmentx_benchmarks \
  --benchmark_min_time=0.01s --benchmark_repetitions=1
```

The suite currently measures repeated allocate/free pairs at fixed sizes for
the system allocator and FragmentX's allocator. It checks that the harness
builds and runs; the short default command is not a publishable performance
claim. For meaningful comparisons, use a dedicated machine, longer runs,
multiple repetitions, and record the compiler, OS, CPU, and build flags.
