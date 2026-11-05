#include <gtest/gtest.h>

#include "analysis/trace_parser.hpp"
#include "analysis/workload_model.hpp"

using namespace fragmentx;
using namespace fragmentx::analysis;

TEST(WorkloadModelTest, EmptyDatabaseProducesEmptyModel) {
  const AllocationDatabase database;
  const auto model = WorkloadModelBuilder::build(database);

  EXPECT_EQ(model.total_events, 0u);
  EXPECT_EQ(model.total_allocations, 0u);
  EXPECT_EQ(model.total_deallocations, 0u);
  EXPECT_TRUE(model.sites.empty());
  EXPECT_TRUE(model.sites_by_bytes.empty());
  EXPECT_TRUE(model.sites_by_count.empty());
  EXPECT_EQ(model.mean_alloc_rate_per_s, 0.0);
}

TEST(WorkloadModelTest, BuildsPerSiteAndGlobalSummaries) {
  AllocationDatabase database;
  database.total_allocs = 3;
  database.total_frees = 2;
  database.unmatched_allocs = 1;
  database.total_requested_bytes = 80;

  AllocationEvent first{};
  first.address = 0x1000;
  first.requested_size = 16;
  first.site_hash = 10;
  first.thread_id = 1;
  first.timestamp_ns = 100;
  first.lifetime_ns = 50;
  first.lifetime_ns = 50;

  AllocationEvent second{};
  second.address = 0x2000;
  second.requested_size = 64;
  second.site_hash = 20;
  second.thread_id = 2;
  second.timestamp_ns = 200;

  AllocationEvent third = first;
  third.address = 0x3000;
  third.requested_size = 32;
  third.timestamp_ns = 300;
  third.lifetime_ns = 100;
  third.lifetime_ns = 100;
  database.events = {first, second, third};

  const auto model = WorkloadModelBuilder::build(database);

  EXPECT_EQ(model.total_events, 3u);
  EXPECT_EQ(model.total_allocations, 3u);
  EXPECT_EQ(model.total_deallocations, 2u);
  EXPECT_EQ(model.unmatched_allocations, 1u);
  EXPECT_EQ(model.total_requested_bytes, 80u);
  EXPECT_EQ(model.trace_duration_ns, 200u);
  EXPECT_EQ(model.distinct_threads, 2u);
  ASSERT_EQ(model.sites.size(), 2u);

  const auto* site10 = model.site(10);
  ASSERT_NE(site10, nullptr);
  EXPECT_EQ(site10->total_allocations, 2u);
  EXPECT_EQ(site10->total_bytes, 48u);
  EXPECT_EQ(site10->live_allocations, 0u);
  EXPECT_EQ(site10->lifetime.sample_count, 2u);
  EXPECT_DOUBLE_EQ(site10->lifetime.mean_ns, 75.0);
  EXPECT_EQ(site10->thread_count, 1u);

  const auto* site20 = model.site(20);
  ASSERT_NE(site20, nullptr);
  EXPECT_EQ(site20->total_allocations, 1u);
  EXPECT_EQ(site20->total_bytes, 64u);
  EXPECT_EQ(site20->live_allocations, 1u);
  EXPECT_EQ(site20->lifetime.sample_count, 0u);

  ASSERT_EQ(model.sites_by_bytes.size(), 2u);
  EXPECT_EQ(model.sites_by_bytes.front(), 20u);
  ASSERT_EQ(model.sites_by_count.size(), 2u);
  EXPECT_EQ(model.sites_by_count.front(), 10u);
  EXPECT_DOUBLE_EQ(model.mean_alloc_rate_per_s, 15000000.0);
}
