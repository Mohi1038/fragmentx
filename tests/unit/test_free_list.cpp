#include <gtest/gtest.h>
#include <runtime/free_list.hpp>
#include <vector>

using namespace fragmentx::runtime;

TEST(FreeListTest, PushAndPopOrder) {
  FreeList list;
  EXPECT_TRUE(list.empty());

  alignas(16) char block1[64];
  alignas(16) char block2[64];

  list.push(block1);
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(list.size(), 1u);

  list.push(block2);
  EXPECT_EQ(list.size(), 2u);

  // LIFO order
  void* p1 = list.pop();
  EXPECT_EQ(p1, static_cast<void*>(block2));

  void* p2 = list.pop();
  EXPECT_EQ(p2, static_cast<void*>(block1));

  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.pop(), nullptr);
}

TEST(SegregatedFreeListTest, MultipleBuckets) {
  SegregatedFreeList seg;
  alignas(16) char b8[8];
  alignas(16) char b16[16];

  seg.push(0, b8);
  seg.push(1, b16);

  EXPECT_EQ(seg.size(0), 1u);
  EXPECT_EQ(seg.size(1), 1u);

  EXPECT_EQ(seg.pop(0), static_cast<void*>(b8));
  EXPECT_EQ(seg.pop(1), static_cast<void*>(b16));
  EXPECT_EQ(seg.pop(0), nullptr);
}

