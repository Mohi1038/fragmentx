#include <gtest/gtest.h>
#include <runtime/arena_manager.hpp>

using namespace fragmentx::runtime;
using namespace fragmentx;

TEST(ArenaManagerTest, CreateAndAllocate) {
  RegionManager rm;
  ArenaManager am(rm, 4 * 1024 * 1024);

  ArenaConfig cfg;
  cfg.type = ArenaType::GeneralPurpose;
  cfg.label = "test_arena";

  uint32_t id = am.create_arena(cfg);
  EXPECT_NE(id, UINT32_MAX);

  Arena* a = am.get_arena(id);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->id, id);

  void* p = a->allocate(64, 16);
  EXPECT_NE(p, nullptr);

  a->deallocate(p, SizeClassManager::class_index(64));

  // Should reuse same pointer
  void* recycled = a->allocate(64, 16);
  EXPECT_EQ(recycled, p);

  am.destroy_arena(id);
  EXPECT_EQ(am.get_arena(id), nullptr);
}

