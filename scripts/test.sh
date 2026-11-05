#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build_test"
printf "\033[36m==> Configuring and running FragmentX test suite...\033[0m\n"

cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DFRAGMENTX_BUILD_TESTS=ON \
      -DFRAGMENTX_BUILD_EXAMPLES=OFF

cmake --build "${BUILD_DIR}" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

ctest --test-dir "${BUILD_DIR}" --output-on-failure
printf "\033[32m✔ All tests passed!\033[0m\n"

