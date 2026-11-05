#include <gtest/gtest.h>
#include <runtime/size_classes.hpp>

using namespace fragmentx::runtime;

TEST(SizeClassTest, ClassLookupAndMonotonicity) {
  EXPECT_EQ(SizeClassManager::class_index(1), 0u);
  EXPECT_EQ(SizeClassManager::class_index(8), 0u);
  EXPECT_EQ(SizeClassManager::class_index(9), 1u);
  EXPECT_EQ(SizeClassManager::class_index(16), 1u);
  EXPECT_EQ(SizeClassManager::class_index(4096), 17u);
  EXPECT_EQ(SizeClassManager::class_index(4097), NUM_SIZE_CLASSES);

  size_t last = 0;
  for (size_t s = 1; s <= 4096; ++s) {
    size_t rounded = SizeClassManager::round_up(s);
    EXPECT_GE(rounded, s);
    EXPECT_GE(rounded, last);
    last = rounded;
  }
}

TEST(SizeClassTest, InternalFragmentationBounds) {
  for (size_t s = 1; s <= 4096; ++s) {
    size_t rounded = SizeClassManager::round_up(s);
    double frag = SizeClassManager::internal_fragmentation(s, rounded);
    EXPECT_GE(frag, 0.0);
    EXPECT_LT(frag, 1.0);
  }
}

TEST(SizeClassTest, AlignmentIsPowerOfTwo) {
  for (uint32_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
    size_t align = SizeClassManager::alignment_for(i);
    EXPECT_TRUE((align & (align - 1)) == 0);
  }
}

