#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build_bench"
printf "\033[36m==> Building and running FragmentX benchmarks...\033[0m\n"

cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DFRAGMENTX_BUILD_BENCHMARKS=ON \
      -DFRAGMENTX_BUILD_EXAMPLES=OFF

cmake --build "${BUILD_DIR}" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

printf "\033[32m✔ Benchmarks built successfully.\033[0m\n"
if [ -f "${BUILD_DIR}/benchmarks/fragmentx_benchmarks" ]; then
  "${BUILD_DIR}/benchmarks/fragmentx_benchmarks"
fi

