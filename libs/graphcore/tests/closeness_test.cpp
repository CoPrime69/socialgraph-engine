#include <gtest/gtest.h>

#include <cmath>

#include "sg/graph/centrality.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

void expect_near_all(const std::vector<double>& actual, const std::vector<double>& expected,
                     double tolerance, const std::string& context = {}) {
  ASSERT_EQ(actual.size(), expected.size()) << context;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    EXPECT_NEAR(actual[i], expected[i], tolerance) << context << " node " << i;
  }
}

TEST(Closeness, EmptyAndSingleNode) {
  EXPECT_TRUE(closeness(CsrGraph{}).empty());
  EXPECT_EQ(closeness(CsrGraph::from_pairs(1, {})), std::vector<double>{0.0});
}

TEST(Closeness, PathHandComputed) {
  // P5: distance sums 10, 7, 6, 7, 10 → 4/D.
  expect_near_all(closeness(testing::path_graph(5), 1), {0.4, 4.0 / 7.0, 4.0 / 6.0, 4.0 / 7.0, 0.4}, 1e-15);
}

TEST(Closeness, StarAndCompleteGraph) {
  // Star with 5 nodes: centre 4/4, leaf 4/(1 + 2·3).
  expect_near_all(closeness(testing::star_graph(5), 1), {1.0, 4.0 / 7.0, 4.0 / 7.0, 4.0 / 7.0, 4.0 / 7.0}, 1e-15);
  for (const double c : closeness(testing::complete_graph(9))) {
    EXPECT_DOUBLE_EQ(c, 1.0);
  }
}

TEST(Closeness, TwoCliquesWithBridge) {
  // K4 ∪ K4 + bridge 3–4. Node 0: 3·1 (own clique) + 1·2 (node 4) + 3·3 = 14 → 7/14.
  // Node 3: 3·1 + 1 (node 4) + 3·2 = 10 → 7/10.
  const auto c = closeness(testing::two_cliques_with_bridge(4));
  EXPECT_NEAR(c[0], 0.5, 1e-15);
  EXPECT_NEAR(c[3], 0.7, 1e-15);
  EXPECT_NEAR(c[4], 0.7, 1e-15);
  EXPECT_NEAR(c[7], 0.5, 1e-15);
}

TEST(Closeness, WassermanFaustOnDisconnectedGraphs) {
  // Path 0-1-2, edge 3-4, isolated 5 (n = 6).
  // Node 0: r = 2, D = 3 → (2/3)(2/5); node 1: r = 2, D = 2 → (2/5); node 3: (1/1)(1/5); node 5: 0.
  const CsrGraph g = testing::make_graph(6, {{0, 1}, {1, 2}, {3, 4}});
  expect_near_all(closeness(g, 1), {4.0 / 15.0, 0.4, 4.0 / 15.0, 0.2, 0.2, 0.0}, 1e-15);
}

TEST(Closeness, NeverInfiniteOrNaN) {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    for (const double c : closeness(testing::random_graph(50, 0.03, seed))) {
      EXPECT_TRUE(std::isfinite(c));
      EXPECT_GE(c, 0.0);
      EXPECT_LE(c, 1.0);
    }
  }
}

TEST(Closeness, MatchesFloydWarshallReferenceOnRandomGraphs) {
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    // Sizes straddle the 64-source batch width of the bit-parallel BFS.
    const std::size_t n = 2 + (seed * 7) % 140;
    const double p = (seed % 3 == 0) ? 0.01 : (seed % 3 == 1 ? 0.04 : 0.15);
    const CsrGraph g = testing::random_graph(n, p, seed);
    expect_near_all(closeness(g, 1), testing::reference_closeness(g), 1e-12, "seed " + std::to_string(seed));
  }
}

TEST(Closeness, ThreadCountDoesNotChangeTheResult) {
  const CsrGraph g = testing::random_graph(700, 0.006, 17);
  const auto reference = closeness(g, 1);
  for (const unsigned threads : {0U, 2U, 3U, 4U, 9U}) {
    EXPECT_EQ(closeness(g, threads), reference) << threads;
  }
}

TEST(Closeness, LongPathNeedsManyLevels) {
  // 300-node path: a batch keeps sweeping for ~300 levels; exercises the bit-sliced counters.
  const CsrGraph g = testing::path_graph(300);
  expect_near_all(closeness(g, 2), testing::reference_closeness(g), 1e-12);
}

TEST(DegreeCentrality, Basics) {
  EXPECT_TRUE(degree_centrality(CsrGraph{}).empty());
  EXPECT_EQ(degree_centrality(CsrGraph::from_pairs(1, {})), std::vector<double>{0.0});
  const auto star = degree_centrality(testing::star_graph(5));
  EXPECT_DOUBLE_EQ(star[0], 1.0);
  EXPECT_DOUBLE_EQ(star[1], 0.25);
  const auto path = degree_centrality(testing::path_graph(3));
  EXPECT_EQ(path, (std::vector<double>{0.5, 1.0, 0.5}));
}

}  // namespace
}  // namespace sg::graph
