#!/usr/bin/env bash
set -euo pipefail

printf "\033[36m=================================================================\033[0m\n"
printf "\033[36m             FragmentX End-to-End Autonomous Demo                \033[0m\n"
printf "\033[36m=================================================================\033[0m\n\n"

# Step 1: Build
./scripts/build.sh Release

BIN="./build/src/cli/fragmentx"
GENERATOR="./build/examples/mixed_lifetime/mixed_lifetime"
TRACE="demo_workload.fxtrace"
POLICY="demo_policy.json"
REPORT="demo_report.html"

# Step 2: System Health Check
printf "\n\033[33m==> Step 1: Running System Environment Diagnostics...\033[0m\n"
"${BIN}" doctor

# Step 3: Generate Canonical Workload
printf "\n\033[33m==> Step 2: Generating Canonical Mixed-Lifetime Workload...\033[0m\n"
"${GENERATOR}" "${TRACE}"

# Step 4: Analyze Workload
printf "\n\033[33m==> Step 3: Analyzing Workload & Diagnosing Heap Inefficiencies...\033[0m\n"
"${BIN}" analyze "${TRACE}"

# Step 5: APM Policy Optimization
printf "\n\033[33m==> Step 4: Running APM Optimizer to Synthesize Optimal Placement Policy...\033[0m\n"
"${BIN}" optimize "${TRACE}" --output "${POLICY}"

# Step 6: Benchmark & Verify
printf "\n\033[33m==> Step 5: Benchmarking Synthesized Policy against Baseline Allocator...\033[0m\n"
"${BIN}" benchmark "${TRACE}" --policy "${POLICY}"

# Step 7: Generate Interactive HTML Report
printf "\n\033[33m==> Step 6: Generating Comprehensive HTML Report...\033[0m\n"
"${BIN}" report "${TRACE}" --output "${REPORT}"

# Step 8: Inspect Policy
printf "\n\033[33m==> Step 7: Inspecting Generated Policy Metadata...\033[0m\n"
"${BIN}" policy inspect "${POLICY}"

printf "\n\033[32m=================================================================\033[0m\n"
printf "\033[32m✔ FragmentX Demo Complete! Open %s to view the full report.\033[0m\n" "${REPORT}"
printf "\033[32m=================================================================\033[0m\n"

