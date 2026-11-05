#include <gtest/gtest.h>

#include <fragmentx/policy.hpp>
#include <limits>

using namespace fragmentx;

TEST(PolicySerializerTest, SerializesPolicyForParsing) {
  AllocationPolicy policy;
  policy.name = "round trip";
  policy.description = "quote: \" and slash: \\";
  policy.strategy = "test";

  PolicyArena arena{};
  arena.id = 7;
  arena.config.type = ArenaType::FixedPool;
  arena.config.initial_size = 4096;
  arena.config.object_size = 64;
  arena.config.label = "test arena";
  arena.rationale = "unit: \"test\"\\reason";
  policy.arenas.push_back(arena);
  policy.arena_assignments.push_back({0x1234, 7, 0.9});

  const auto json = PolicySerializer::to_json(policy, false);
  ASSERT_FALSE(json.empty());
  const auto parsed = PolicyLoader::parse(json);
  ASSERT_TRUE(parsed.has_value()) << PolicyLoader::last_error();
  EXPECT_EQ(parsed->name, policy.name);
  EXPECT_EQ(parsed->description, policy.description);
  EXPECT_EQ(parsed->arena_for_site(0x1234), 7u);
  ASSERT_EQ(parsed->arenas.size(), 1u);
  EXPECT_EQ(parsed->arenas.front().config.type, ArenaType::FixedPool);
  EXPECT_EQ(parsed->arenas.front().config.initial_size, 4096u);
  EXPECT_EQ(parsed->arenas.front().config.object_size, 64u);
  EXPECT_EQ(parsed->arenas.front().config.label, "test arena");
  EXPECT_EQ(parsed->arenas.front().rationale, arena.rationale);
}

TEST(PolicySerializerTest, RejectsMalformedInputWithoutThrowing) {
  EXPECT_FALSE(PolicyLoader::parse("not-json").has_value());
  EXPECT_FALSE(PolicyLoader::parse("{\"version\":\"1\",\"arenas\":[{\"id\":\"bad\"}],\"assignments\":[]}").has_value());
  EXPECT_FALSE(PolicyLoader::last_error().empty());
}

TEST(PolicySerializerTest, RejectsNonFiniteJsonNumbers) {
  AllocationPolicy policy;
  policy.cost_value = std::numeric_limits<double>::infinity();
  EXPECT_TRUE(PolicySerializer::to_json(policy).empty());
}
