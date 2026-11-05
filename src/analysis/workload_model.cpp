/// @file workload_model.cpp
/// @brief Builds a WorkloadModel from an AllocationDatabase.

#include "workload_model.hpp"
#include "trace_parser.hpp"
#include <fragmentx/workload.hpp>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace fragmentx {
namespace analysis {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static double compute_entropy(const std::unordered_map<uint64_t, uint64_t>& dist) {
  uint64_t total = 0;
  for (auto& [k, v] : dist) total += v;
  if (!total) return 0.0;
  double H = 0.0;
  for (auto& [k, v] : dist) {
    if (!v) continue;
    double p = static_cast<double>(v) / static_cast<double>(total);
    H -= p * std::log2(p);
  }
  return H;
}

static double compute_cv(const std::vector<double>& vals) {
  if (vals.size() < 2) return 0.0;
  double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
  if (mean == 0.0) return 0.0;
  double var = 0.0;
  for (double v : vals) var += (v - mean) * (v - mean);
  var /= (vals.size() - 1);
  return std::sqrt(var) / mean;
}

static double compute_burstiness(const std::vector<uint64_t>& timestamps_ns) {
  if (timestamps_ns.size() < 2) return 0.0;
  std::vector<double> iats;
  iats.reserve(timestamps_ns.size() - 1);
  for (std::size_t i = 1; i < timestamps_ns.size(); ++i) {
    if (timestamps_ns[i] > timestamps_ns[i-1])
      iats.push_back(static_cast<double>(timestamps_ns[i] - timestamps_ns[i-1]));
  }
  return std::min(compute_cv(iats), 1.0);
}

// ─── WorkloadModelBuilder ────────────────────────────────────────────────────

WorkloadModel WorkloadModelBuilder::build(const AllocationDatabase& db) {
  WorkloadModel model;
  model.total_events         = db.events.size();
  model.total_allocations    = db.total_allocs;
  model.total_deallocations  = db.total_frees;
  model.unmatched_allocations= db.unmatched_allocs;
  model.total_requested_bytes= db.total_requested_bytes;

  // Accumulate per-site data.
  std::unordered_map<uint64_t, std::vector<uint64_t>> site_timestamps;
  std::unordered_map<uint64_t, std::vector<uint64_t>> site_lifetimes;
  std::unordered_map<uint64_t, std::vector<uint64_t>> site_sizes;

  uint64_t min_ts = UINT64_MAX, max_ts = 0;

  for (const auto& ev : db.events) {
    auto& profile = model.sites[ev.site_hash];
    profile.site_hash = ev.site_hash;
    profile.total_allocations++;
    profile.total_bytes += ev.requested_size;
    if (ev.is_freed())
      site_lifetimes[ev.site_hash].push_back(ev.lifetime_ns);
    else
      profile.live_allocations++;

    site_timestamps[ev.site_hash].push_back(ev.timestamp_ns);
    site_sizes[ev.site_hash].push_back(ev.requested_size);
    profile.thread_distribution[ev.thread_id]++;
    profile.thread_count = static_cast<uint32_t>(
        profile.thread_distribution.size());

    min_ts = std::min(min_ts, ev.timestamp_ns);
    max_ts = std::max(max_ts, ev.timestamp_ns);

    // Size histogram.
    uint32_t bucket = 0;
    uint64_t sz = ev.requested_size;
    while (sz > 1 && bucket < 31) { sz >>= 1; ++bucket; }
    profile.size_dist.histogram[bucket]++;
  }

  model.trace_duration_ns = (max_ts > min_ts) ? max_ts - min_ts : 0;

  // Finalize per-site profiles.
  for (auto& [hash, profile] : model.sites) {
    // Size stats.
    auto& sizes = site_sizes[hash];
    if (!sizes.empty()) {
      std::sort(sizes.begin(), sizes.end());
      double sum = std::accumulate(sizes.begin(), sizes.end(), 0.0);
      profile.size_dist.mean_bytes = sum / sizes.size();
      profile.size_dist.min_bytes  = sizes.front();
      profile.size_dist.max_bytes  = sizes.back();
      profile.size_dist.p50_bytes  = sizes[sizes.size() / 2];
      profile.size_dist.p95_bytes  = sizes[sizes.size() * 95 / 100];
      std::vector<double> dsizes(sizes.begin(), sizes.end());
      profile.size_dist.cv = compute_cv(dsizes);
    }

    // Lifetime stats.
    auto& lts = site_lifetimes[hash];
    if (!lts.empty()) {
      std::sort(lts.begin(), lts.end());
      double sum = std::accumulate(lts.begin(), lts.end(), 0.0);
      profile.lifetime.mean_ns     = sum / lts.size();
      profile.lifetime.min_ns      = static_cast<double>(lts.front());
      profile.lifetime.max_ns      = static_cast<double>(lts.back());
      profile.lifetime.p50_ns      = static_cast<double>(lts[lts.size() / 2]);
      profile.lifetime.p95_ns      = static_cast<double>(lts[lts.size() * 95 / 100]);
      profile.lifetime.p99_ns      = static_cast<double>(lts[lts.size() * 99 / 100]);
      profile.lifetime.sample_count= lts.size();
      profile.lifetime.confidence  = std::min(1.0, lts.size() / 100.0);
      std::vector<double> dlts(lts.begin(), lts.end());
      profile.lifetime.cv          = compute_cv(dlts);
      // Classify distribution.
      if (profile.lifetime.cv > 1.5)
        profile.lifetime.distribution_type = DistributionType::HeavyTailed;
      else if (profile.lifetime.cv > 0.8)
        profile.lifetime.distribution_type = DistributionType::Exponential;
      else if (profile.lifetime.cv < 0.2)
        profile.lifetime.distribution_type = DistributionType::Normal;
      else
        profile.lifetime.distribution_type = DistributionType::Unknown;
    }

    // Burstiness.
    auto& ts_vec = site_timestamps[hash];
    std::sort(ts_vec.begin(), ts_vec.end());
    profile.burstiness = compute_burstiness(ts_vec);

    // Thread entropy.
    profile.thread_entropy = compute_entropy(profile.thread_distribution);
  }

  // Build sorted site lists.
  for (auto& [h, p] : model.sites) {
    model.sites_by_bytes.push_back(h);
    model.sites_by_count.push_back(h);
  }
  std::sort(model.sites_by_bytes.begin(), model.sites_by_bytes.end(),
      [&](uint64_t a, uint64_t b) {
        return model.sites[a].total_bytes > model.sites[b].total_bytes;
      });
  std::sort(model.sites_by_count.begin(), model.sites_by_count.end(),
      [&](uint64_t a, uint64_t b) {
        return model.sites[a].total_allocations > model.sites[b].total_allocations;
      });

  model.distinct_threads = 0;
  std::unordered_map<uint64_t, bool> seen_threads;
  for (auto& ev : db.events)
    seen_threads[ev.thread_id] = true;
  model.distinct_threads = static_cast<uint32_t>(seen_threads.size());

  if (model.trace_duration_ns > 0) {
    model.mean_alloc_rate_per_s =
        static_cast<double>(model.total_allocations)
      / (static_cast<double>(model.trace_duration_ns) / 1e9);
  }

  return model;
}

} // namespace analysis
} // namespace fragmentx

