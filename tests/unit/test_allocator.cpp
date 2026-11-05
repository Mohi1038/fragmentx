#include <gtest/gtest.h>
#include <cstring>
#include <fragmentx/allocator.hpp>

using namespace fragmentx;

TEST(AllocatorTest, BasicLifecycle) {
  Config cfg;
  cfg.region_size = 4 * 1024 * 1024;
  Allocator alloc(cfg);

  void* p = alloc.allocate(128);
  ASSERT_NE(p, nullptr);

  // Alignment test
  EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0u);

  std::memset(p, 0x42, 128);
  EXPECT_EQ(static_cast<uint8_t*>(p)[0], 0x42);

  alloc.deallocate(p);

  auto stats = alloc.statistics();
  EXPECT_EQ(stats.total_allocations, 1u);
  EXPECT_EQ(stats.total_deallocations, 1u);
}

TEST(AllocatorTest, Reallocate) {
  Config cfg;
  Allocator alloc(cfg);

  void* p = alloc.allocate(32);
  ASSERT_NE(p, nullptr);
  std::strcpy(static_cast<char*>(p), "Hello FragmentX");

  void* p2 = alloc.reallocate(p, 256);
  ASSERT_NE(p2, nullptr);
  EXPECT_STREQ(static_cast<char*>(p2), "Hello FragmentX");

  alloc.deallocate(p2);
}

