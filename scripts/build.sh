#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

printf "\033[36m==> Building FragmentX (%s)...\033[0m\n" "${BUILD_TYPE}"

cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DFRAGMENTX_BUILD_EXAMPLES=ON \
      -DFRAGMENTX_BUILD_TESTS=OFF \
      -DFRAGMENTX_BUILD_BENCHMARKS=OFF

cmake --build "${BUILD_DIR}" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

printf "\033[32m✔ FragmentX built successfully in %s/\033[0m\n" "${BUILD_DIR}"
printf "Binary: %s/src/cli/fragmentx\n" "${BUILD_DIR}"

