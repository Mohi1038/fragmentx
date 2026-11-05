#include "candidate_generator.hpp"
#include "compatibility.hpp"
#include <algorithm>

namespace fragmentx {
namespace optimizer {

AllocationPolicy CandidateGenerator::make_baseline() {
  AllocationPolicy p;
  p.name        = "Baseline (default routing)";
  p.strategy    = "Baseline";
  p.description = "No optimization applied. All allocations use the default arena.";
  p.cost_value  = 1.0;

  PolicyArena arena;
  arena.id = 1;
  arena.config.type  = ArenaType::GeneralPurpose;
  arena.config.label = "default";
  arena.rationale    = "Default general-purpose arena.";
  p.arenas.push_back(arena);
  return p;
}

AllocationPolicy CandidateGenerator::make_lifetime_grouped(
    const std::vector<FeatureVector>& features)
{
  AllocationPolicy p;
  p.name     = "LifetimeGrouped";
  p.strategy = "LifetimeGrouped";
  p.description =
      "Groups allocation sites into short-lived, medium-lived, and long-lived arenas "
      "based on mean lifetime. Reduces lifetime-mixing fragmentation.";

  // 1: short (<1ms), 2: medium (1ms - 1s), 3: long (>1s)
  PolicyArena a1{1, {ArenaType::Region, 0, 0, 0, 0, "short-lived"}, "Short-lived bump/region arena."};
  PolicyArena a2{2, {ArenaType::GeneralPurpose, 0, 0, 0, 0, "medium-lived"}, "Medium-lived free-list arena."};
  PolicyArena a3{3, {ArenaType::LongLived, 0, 0, 0, 0, "long-lived"}, "Persistent / long-lived arena."};
  p.arenas = {a1, a2, a3};

  for (const auto& fv : features) {
    uint32_t arena_id = 2;
    if (fv.mean_lifetime < 1'000'000ULL)   arena_id = 1; // < 1ms
    else if (fv.mean_lifetime > 1'000'000'000ULL) arena_id = 3; // > 1s

    p.arena_assignments.push_back({fv.site_hash, arena_id, 0.75});
  }
  return p;
}

AllocationPolicy CandidateGenerator::make_compatibility_grouped(
    const std::vector<FeatureVector>& features,
    const CompatibilityWeights& weights)
{
  AllocationPolicy p;
  p.name     = "CompatibilityGrouped";
  p.strategy = "CompatibilityGrouped";
  p.description =
      "Groups allocation sites by multi-signal compatibility score. "
      "Compatible sites share arenas to improve locality and reclaimability.";

  const size_t N = features.size();
  std::vector<uint32_t> assignment(N, 0);
  uint32_t next_arena = 1;

  for (size_t i = 0; i < N; ++i) {
    if (assignment[i] != 0) continue;
    assignment[i] = next_arena++;
    for (size_t j = i + 1; j < N; ++j) {
      if (assignment[j] != 0) continue;
      auto score = CompatibilityEngine::score_pair(features[i], features[j], weights);
      if (score.can_share_arena) {
        assignment[j] = assignment[i];
      }
    }
  }

  uint32_t max_arena = assignment.empty() ? 1 : *std::max_element(assignment.begin(), assignment.end());
  for (uint32_t id = 1; id <= max_arena; ++id) {
    PolicyArena ar;
    ar.id = id;
    ar.config.type  = ArenaType::GeneralPurpose;
    ar.config.label = "compat-group-" + std::to_string(id);
    ar.rationale    = "Compatibility-grouped arena " + std::to_string(id);
    p.arenas.push_back(ar);
  }

  for (size_t i = 0; i < N; ++i) {
    p.arena_assignments.push_back({features[i].site_hash, assignment[i], 0.68});
  }
  return p;
}

AllocationPolicy CandidateGenerator::make_size_optimized(
    const std::vector<FeatureVector>& features)
{
  AllocationPolicy p;
  p.name     = "SizeClassOptimized";
  p.strategy = "SizeClassOptimized";
  p.description = "Partitions allocation sites by object size range to minimize size-class padding waste.";

  PolicyArena a1{1, {ArenaType::FixedPool, 64, 0, 0, 0, "tiny-pool"}, "Pool for small blocks <= 64B."};
  PolicyArena a2{2, {ArenaType::GeneralPurpose, 0, 0, 0, 0, "mid-range"}, "Arena for medium blocks 64B - 512B."};
  PolicyArena a3{3, {ArenaType::GeneralPurpose, 0, 0, 0, 0, "large-range"}, "Arena for large blocks > 512B."};
  p.arenas = {a1, a2, a3};

  for (const auto& fv : features) {
    uint32_t aid = 2;
    if (fv.mean_size <= 64)       aid = 1;
    else if (fv.mean_size > 512)  aid = 3;
    p.arena_assignments.push_back({fv.site_hash, aid, 0.65});
  }
  return p;
}

AllocationPolicy CandidateGenerator::make_thread_affinity(
    const std::vector<FeatureVector>& features)
{
  AllocationPolicy p;
  p.name     = "ThreadAffinity";
  p.strategy = "ThreadAffinity";
  p.description = "Separates thread-private allocations into dedicated arenas to eliminate lock contention.";

  PolicyArena a1{1, {ArenaType::GeneralPurpose, 0, 0, 0, 0, "low-concurrency"}, "Single-threaded / private arena."};
  PolicyArena a2{2, {ArenaType::GeneralPurpose, 0, 0, 0, 0, "high-concurrency"}, "Shared multi-threaded arena."};
  p.arenas = {a1, a2};

  for (const auto& fv : features) {
    uint32_t aid = (fv.thread_entropy > 1.0) ? 2 : 1;
    p.arena_assignments.push_back({fv.site_hash, aid, 0.70});
  }
  return p;
}

std::vector<AllocationPolicy> CandidateGenerator::generate_all(
    const std::vector<FeatureVector>& features,
    const CompatibilityWeights& weights)
{
  std::vector<AllocationPolicy> list;
  if (features.empty()) {
    list.push_back(make_baseline());
    return list;
  }
  list.push_back(make_lifetime_grouped(features));
  list.push_back(make_compatibility_grouped(features, weights));
  list.push_back(make_size_optimized(features));
  list.push_back(make_thread_affinity(features));
  return list;
}

} // namespace optimizer
} // namespace fragmentx

