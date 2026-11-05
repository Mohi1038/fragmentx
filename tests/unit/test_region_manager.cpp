#include <gtest/gtest.h>
#include <runtime/region_manager.hpp>
#include <cstring>

using namespace fragmentx::runtime;

TEST(RegionManagerTest, AcquireAndRelease) {
  RegionManager rm;
  size_t req_size = 4 * 1024 * 1024; // 4MB

  Region* r = rm.acquire(req_size, RegionType::Small);
  ASSERT_NE(r, nullptr);
  EXPECT_NE(r->base, nullptr);
  EXPECT_GE(r->size, req_size);
  EXPECT_TRUE(r->in_use);

  // Bump allocate from region
  void* p1 = r->bump_alloc(128, 16);
  EXPECT_NE(p1, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(p1) % 16, 0u);

  // Write test to ensure memory is backed
  std::memset(p1, 0xAB, 128);
  EXPECT_EQ(static_cast<uint8_t*>(p1)[0], 0xAB);

  rm.release(r);
  auto stats = rm.statistics();
  EXPECT_EQ(stats.regions_acquired, 1u);
  EXPECT_EQ(stats.regions_released, 1u);
}

