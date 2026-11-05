#include <gtest/gtest.h>

#include "optimizer/compatibility.hpp"

using namespace fragmentx;
using namespace fragmentx::optimizer;

TEST(CompatibilityTest, IdenticalFeaturesReceiveMaximumScore) {
  FeatureVector first{};
  first.site_hash = 11;
  first.mean_size = 64.0;
  first.mean_lifetime = 1000.0;
  first.burstiness = 0.2;
  first.thread_entropy = 1.0;
  first.reuse_rate = 0.5;
  const FeatureVector second = first;

  const auto score = CompatibilityEngine::score_pair(
      first, second, CompatibilityWeights{});

  EXPECT_EQ(score.site_a, 11u);
  EXPECT_EQ(score.site_b, 11u);
  EXPECT_DOUBLE_EQ(score.total, 1.0);
  EXPECT_TRUE(score.can_share_arena);
}

TEST(CompatibilityTest, ComputesEachUnorderedPairOnce) {
  const std::vector<FeatureVector> features(3);
  const auto scores = CompatibilityEngine::compute_all_pairs(
      features, CompatibilityWeights{});
  EXPECT_EQ(scores.size(), 3u);
}
