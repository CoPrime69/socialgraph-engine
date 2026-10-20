#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

#include "sg/graph/generator.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

using Edge = std::pair<NodeId, NodeId>;

// Golden values below were produced by an independent Python implementation of the contract
// (SplitMix64 + Lemire + contracts.md §8), not by this library.

TEST(SplitMix64, MatchesReferenceSequence) {
  // Reference vector of Vigna's splitmix64.c for seed 1234567.
  SplitMix64 rng(1234567);
  const std::array<std::uint64_t, 5> expected = {6457827717110365317ULL, 3203168211198807973ULL,
                                                 9817491932198370423ULL, 4593380528125082431ULL,
                                                 16408922859458223821ULL};
  for (const std::uint64_t value : expected) {
    EXPECT_EQ(rng.next(), value);
  }
  SplitMix64 zero(0);
  EXPECT_EQ(zero.next(), 0xE220A8397B1DCDAFULL);
}

TEST(SplitMix64, BoundedIsLemireAndInRange) {
  SplitMix64 rng(42);
  const std::array<std::uint64_t, 10> expected = {7, 1, 2, 3, 0, 8, 2, 8, 3, 6};
  for (const std::uint64_t value : expected) {
    EXPECT_EQ(rng.bounded(10), value);
  }
  // A bound just above 2^63 rejects about half the draws: exercises the slow path.
  SplitMix64 big(42);
  const std::uint64_t n = (std::uint64_t{1} << 63U) + 1;
  const std::array<std::uint64_t, 4> expected_big = {1474913046063446145ULL, 8007990562831494531ULL,
                                                     2014432356388812462ULL, 7384525663493887954ULL};
  for (const std::uint64_t value : expected_big) {
    EXPECT_EQ(big.bounded(n), value);
  }
  EXPECT_THROW((void)rng.bounded(0), std::invalid_argument);
  EXPECT_EQ(rng.bounded(1), 0U);
}

TEST(SplitMix64, BoundedIsRoughlyUniform) {
  SplitMix64 rng(2024);
  constexpr std::uint64_t kBuckets = 7;
  constexpr int kDraws = 70000;
  std::array<int, kBuckets> counts{};
  for (int i = 0; i < kDraws; ++i) {
    ++counts[rng.bounded(kBuckets)];
  }
  // Chi-square with 6 degrees of freedom; 22.46 is the 0.999 quantile.
  const double expected = static_cast<double>(kDraws) / kBuckets;
  double chi2 = 0.0;
  for (const int c : counts) {
    chi2 += (c - expected) * (c - expected) / expected;
  }
  EXPECT_LT(chi2, 22.46);
}

TEST(SplitMix64, Uniform01) {
  SplitMix64 rng(7);
  EXPECT_DOUBLE_EQ(rng.uniform01(), 0.3898297483912715);
  EXPECT_DOUBLE_EQ(rng.uniform01(), 0.01678829452815611);
  EXPECT_DOUBLE_EQ(rng.uniform01(), 0.9007606806068834);
  for (int i = 0; i < 10000; ++i) {
    const double x = rng.uniform01();
    ASSERT_GE(x, 0.0);
    ASSERT_LT(x, 1.0);
  }
}

std::uint64_t fnv1a(const std::vector<Edge>& edges) {
  std::uint64_t h = 0xCBF29CE484222325ULL;
  for (const auto& [u, v] : edges) {
    for (const NodeId x : {u, v}) {
      for (unsigned byte = 0; byte < 4; ++byte) {
        h ^= (x >> (8U * byte)) & 0xFFU;
        h *= 0x100000001B3ULL;
      }
    }
  }
  return h;
}

TEST(Generator, GoldenSeedGraph) {
  // contracts.md §8: the original 100 users grouped by (category, branch), defaults
  // (average degree 8, μ = 0.2, seed 20241109).
  const auto groups = testing::seed_groups(testing::seed_users());
  const auto edges = generate_homophilous_edges(groups);
  ASSERT_EQ(edges.size(), 351U);
  const std::vector<Edge> first = {{0, 9},  {0, 10}, {0, 30}, {0, 75}, {0, 95}, {1, 27},
                                   {1, 47}, {1, 67}, {1, 71}, {1, 81}, {1, 96}, {2, 6}};
  EXPECT_TRUE(std::equal(first.begin(), first.end(), edges.begin()));
  const std::vector<Edge> last = {{91, 96}, {93, 98}, {94, 99}};
  EXPECT_TRUE(std::equal(last.begin(), last.end(), edges.end() - 3));
  EXPECT_EQ(fnv1a(edges), 0x073AB736B467B24CULL);
  const auto inside = std::count_if(edges.begin(), edges.end(),
                                    [&](const Edge& e) { return groups[e.first] == groups[e.second]; });
  EXPECT_EQ(inside, 291);
}

TEST(Generator, GoldenOtherParameters) {
  std::vector<std::uint32_t> groups(50);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>(i % 7);
  }
  const auto edges = generate_homophilous_edges(groups, GeneratorOptions{9.0, 0.5, 99});  // 5 draws each
  ASSERT_EQ(edges.size(), 212U);
  const std::vector<Edge> first = {{0, 6}, {0, 7}, {0, 12}, {0, 14}, {0, 15}, {0, 28}};
  EXPECT_TRUE(std::equal(first.begin(), first.end(), edges.begin()));
}

TEST(Generator, CanonicalNoSelfLoopsNoDuplicates) {
  std::vector<std::uint32_t> groups(500);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>((i * 31) % 17);
  }
  const auto edges = generate_homophilous_edges(groups, GeneratorOptions{10.0, 0.3, 1});
  EXPECT_TRUE(std::is_sorted(edges.begin(), edges.end()));
  EXPECT_EQ(std::adjacent_find(edges.begin(), edges.end()), edges.end());
  for (const auto& [u, v] : edges) {
    EXPECT_LT(u, v);
    EXPECT_LT(v, groups.size());
  }
  // 5 draws per node, a few collide: the realised average degree is just below 10.
  const double average = 2.0 * static_cast<double>(edges.size()) / static_cast<double>(groups.size());
  EXPECT_GT(average, 9.0);
  EXPECT_LE(average, 10.0);
}

TEST(Generator, Deterministic) {
  std::vector<std::uint32_t> groups(300);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>(i / 20);
  }
  const auto a = generate_homophilous_edges(groups);
  EXPECT_EQ(generate_homophilous_edges(groups), a);
  GeneratorOptions other;
  other.seed = 1;
  EXPECT_NE(generate_homophilous_edges(groups, other), a);
}

TEST(Generator, MixingControlsHomophily) {
  std::vector<std::uint32_t> groups(400);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>(i % 10);  // interleaved groups of 40
  }
  auto inside_share = [&](double mixing) {
    const auto edges = generate_homophilous_edges(groups, GeneratorOptions{8.0, mixing, 3});
    const auto inside = std::count_if(edges.begin(), edges.end(),
                                      [&](const Edge& e) { return groups[e.first] == groups[e.second]; });
    return static_cast<double>(inside) / static_cast<double>(edges.size());
  };
  EXPECT_EQ(inside_share(0.0), 1.0);
  EXPECT_NEAR(inside_share(1.0), 0.1, 0.05);  // uniform partners: ~1/10 land in the own group
  EXPECT_NEAR(inside_share(0.2), 0.8 + 0.2 * 0.1, 0.05);
}

TEST(Generator, SingletonGroupsFallBackToUniformPartners) {
  std::vector<std::uint32_t> groups(30);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>(i);  // every node alone in its group
  }
  const auto edges = generate_homophilous_edges(groups, GeneratorOptions{4.0, 0.0, 5});
  EXPECT_FALSE(edges.empty());
  for (const auto& [u, v] : edges) {
    EXPECT_NE(u, v);
  }
}

TEST(Generator, EdgeCases) {
  EXPECT_TRUE(generate_homophilous_edges({}).empty());
  const std::vector<std::uint32_t> one = {3};
  EXPECT_TRUE(generate_homophilous_edges(one).empty());
  const std::vector<std::uint32_t> two = {1, 1};
  EXPECT_EQ(generate_homophilous_edges(two), (std::vector<Edge>{{0, 1}}));
  EXPECT_TRUE(generate_homophilous_edges(two, GeneratorOptions{0.0, 0.2, 1}).empty());
  EXPECT_THROW((void)generate_homophilous_edges(two, GeneratorOptions{-1.0, 0.2, 1}), std::invalid_argument);
  EXPECT_THROW((void)generate_homophilous_edges(two, GeneratorOptions{8.0, 1.5, 1}), std::invalid_argument);
  EXPECT_THROW((void)generate_homophilous_edges(two, GeneratorOptions{NAN, 0.2, 1}), std::invalid_argument);
}

TEST(SeedFixture, MatchesTheOriginalLegacyUserFile) {
  const auto legacy = testing::load_legacy_seed_users();
  if (!legacy) {
    GTEST_SKIP() << "legacy/user_data.csv not available";
  }
  const auto embedded = testing::seed_users();
  ASSERT_EQ(legacy->size(), embedded.size());
  for (std::size_t i = 0; i < embedded.size(); ++i) {
    EXPECT_EQ((*legacy)[i].handle, embedded[i].handle);
    EXPECT_EQ((*legacy)[i].category, embedded[i].category);
    EXPECT_EQ((*legacy)[i].branch, embedded[i].branch);
    EXPECT_EQ((*legacy)[i].influence, embedded[i].influence);
  }
}

}  // namespace
}  // namespace sg::graph
