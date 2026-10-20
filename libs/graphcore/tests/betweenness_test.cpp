#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

#include "sg/graph/centrality.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

BetweennessOptions exact(unsigned threads = 1, bool normalized = true) {
  BetweennessOptions o;
  o.threads = threads;
  o.normalized = normalized;
  return o;
}

void expect_near_all(const std::vector<double>& actual, const std::vector<double>& expected,
                     double tolerance, const std::string& context = {}) {
  ASSERT_EQ(actual.size(), expected.size()) << context;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    EXPECT_NEAR(actual[i], expected[i], tolerance) << context << " node " << i;
  }
}

TEST(Betweenness, EmptyAndTinyGraphsAreZero) {
  EXPECT_TRUE(betweenness(CsrGraph{}).scores.empty());
  EXPECT_EQ(betweenness(CsrGraph::from_pairs(1, {})).scores, std::vector<double>{0.0});
  EXPECT_EQ(betweenness(testing::path_graph(2)).scores, (std::vector<double>{0.0, 0.0}));
  EXPECT_EQ(betweenness(testing::path_graph(2), exact(1, false)).scores, (std::vector<double>{0.0, 0.0}));
}

TEST(Betweenness, PathHandComputed) {
  // P5: node 1 lies on (0,2),(0,3),(0,4); node 2 on (0,3),(0,4),(1,3),(1,4).
  const CsrGraph g = testing::path_graph(5);
  expect_near_all(betweenness(g, exact(1, false)).scores, {0, 3, 4, 3, 0}, 1e-12);
  expect_near_all(betweenness(g).scores, {0, 0.5, 4.0 / 6.0, 0.5, 0}, 1e-12);
}

TEST(Betweenness, StarCentreIsOne) {
  const auto r = betweenness(testing::star_graph(6));
  EXPECT_NEAR(r.scores[0], 1.0, 1e-12);
  for (NodeId leaf = 1; leaf < 6; ++leaf) {
    EXPECT_EQ(r.scores[leaf], 0.0);
  }
  EXPECT_FALSE(r.approximate);
  EXPECT_EQ(r.sources_used, 6U);
}

TEST(Betweenness, CycleIsUniform) {
  // C6: every node is on one shortest path of each adjacent-neighbour pair, half of each of the
  // two shortest paths between opposite pairs, ... the brute force gives 2 per node (unnormalised).
  const auto r = betweenness(testing::cycle_graph(6), exact(1, false));
  for (const double s : r.scores) {
    EXPECT_NEAR(s, 2.0, 1e-12);
  }
}

TEST(Betweenness, CompleteGraphIsZero) {
  for (const double s : betweenness(testing::complete_graph(7)).scores) {
    EXPECT_EQ(s, 0.0);
  }
}

TEST(Betweenness, TwoCliquesJoinedByABridge) {
  // K4 ∪ K4 with bridge 3-4: every one of the 3·4 pairs {0,1,2}×{4..7} passes through 3.
  const auto r = betweenness(testing::two_cliques_with_bridge(4), exact(1, false));
  expect_near_all(r.scores, {0, 0, 0, 12, 12, 0, 0, 0}, 1e-12);
  const auto normalized = betweenness(testing::two_cliques_with_bridge(4));
  EXPECT_NEAR(normalized.scores[3], 12.0 / 21.0, 1e-12);
}

TEST(Betweenness, DisconnectedGraph) {
  // Path 0-1-2, edge 3-4, isolated 5: only node 1 is ever in between.
  const CsrGraph g = testing::make_graph(6, {{0, 1}, {1, 2}, {3, 4}});
  expect_near_all(betweenness(g).scores, {0, 1.0 / 10.0, 0, 0, 0, 0}, 1e-12);
}

TEST(Betweenness, CountsMultipleShortestPathsBySigma) {
  // s=0 → {1,2,3}; 1,2 → 4 (σ4 = 2); 3 → 5 (σ5 = 1); 4,5 → 6 (σ6 = 3).
  // Node 4 carries 2/3 of the 0-6 paths, node 5 carries 1/3.
  const CsrGraph g = testing::make_graph(7, {{0, 1}, {0, 2}, {0, 3}, {1, 4}, {2, 4}, {3, 5}, {4, 6}, {5, 6}});
  const auto r = betweenness(g, exact(1, false));
  expect_near_all(r.scores, testing::brute_force_betweenness(g, false), 1e-12);
}

TEST(Betweenness, MatchesBruteForceOnRandomGraphs) {
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    const std::size_t n = 3 + seed % 38;
    const double p = (seed % 4 == 0) ? 0.06 : (seed % 4 == 1 ? 0.12 : (seed % 4 == 2 ? 0.25 : 0.5));
    const CsrGraph g = testing::random_graph(n, p, seed);
    for (const bool normalized : {false, true}) {
      const auto r = betweenness(g, exact(1, normalized));
      expect_near_all(r.scores, testing::brute_force_betweenness(g, normalized), 1e-9,
                      "seed " + std::to_string(seed));
    }
  }
}

TEST(Betweenness, ThreadCountDoesNotChangeASingleBit) {
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    // > 256 sources so that blocks hold several sources and several rounds are needed.
    const CsrGraph g = testing::random_graph(300 + 40 * seed, 0.02, seed);
    const auto reference = betweenness(g, exact(1)).scores;
    for (const unsigned threads : {2U, 3U, 4U, 7U, 16U}) {
      const auto r = betweenness(g, exact(threads)).scores;
      ASSERT_EQ(r.size(), reference.size());
      for (std::size_t i = 0; i < r.size(); ++i) {
        ASSERT_EQ(r[i], reference[i]) << "threads " << threads << " node " << i;  // bit-identical
      }
    }
    // The automatic thread count gives the same bits as well.
    BetweennessOptions automatic;
    EXPECT_EQ(betweenness(g, automatic).scores, reference);
  }
}

TEST(Betweenness, ThreadCountInvarianceOnSmallGraphsToo) {
  const CsrGraph g = testing::random_graph(37, 0.15, 99);
  const auto reference = betweenness(g, exact(1)).scores;
  for (const unsigned threads : {2U, 4U, 8U}) {
    EXPECT_EQ(betweenness(g, exact(threads)).scores, reference);
  }
}

TEST(Betweenness, SamplingWithAllSourcesIsExact) {
  const CsrGraph g = testing::random_graph(60, 0.08, 5);
  const auto exact_result = betweenness(g, exact());
  BetweennessOptions sampled = exact();
  sampled.sample_sources = 60;  // k == n: exact computation
  const auto r = betweenness(g, sampled);
  EXPECT_FALSE(r.approximate);
  EXPECT_EQ(r.sources_used, 60U);
  EXPECT_EQ(r.scores, exact_result.scores);
  sampled.sample_sources = 1000;  // k > n: also exact
  EXPECT_EQ(betweenness(g, sampled).scores, exact_result.scores);
}

TEST(Betweenness, SamplingIsDeterministicScaledAndThreadInvariant) {
  const CsrGraph g = testing::random_graph(400, 0.015, 21);
  BetweennessOptions o = exact(1);
  o.sample_sources = 50;
  o.seed = 7;
  const auto a = betweenness(g, o);
  EXPECT_TRUE(a.approximate);
  EXPECT_EQ(a.sources_used, 50U);
  EXPECT_EQ(betweenness(g, o).scores, a.scores);  // same seed → same pivots → same bits
  o.threads = 4;
  EXPECT_EQ(betweenness(g, o).scores, a.scores);
  o.seed = 8;
  EXPECT_NE(betweenness(g, o).scores, a.scores);  // different pivots
}

TEST(Betweenness, SamplingScalesByNOverK) {
  // Star: the centre's dependency is (n−2) for every leaf pivot and 0 for the centre itself, so
  // with k leaf pivots the estimate is (n/k)·k·(n−2)/2 = n(n−2)/2 versus the exact (n−1)(n−2)/2.
  const std::size_t n = 50;
  const CsrGraph g = testing::star_graph(n);
  BetweennessOptions o = exact(1, false);
  o.sample_sources = 10;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    o.seed = seed;
    const auto r = betweenness(g, o);
    const double nodes = static_cast<double>(n);
    // Whether the centre (node 0) was drawn changes the estimate; both values are known.
    const double without_centre = nodes * (nodes - 2.0) / 2.0;
    const double with_centre = nodes / 10.0 * 9.0 * (nodes - 2.0) / 2.0;
    EXPECT_TRUE(std::abs(r.scores[0] - without_centre) < 1e-9 || std::abs(r.scores[0] - with_centre) < 1e-9)
        << r.scores[0];
  }
}

TEST(Betweenness, SampledEstimateIsCloseOnAverage) {
  const CsrGraph g = testing::random_graph(300, 0.02, 3);
  const auto truth = betweenness(g, exact(2)).scores;
  std::vector<double> mean(truth.size(), 0.0);
  constexpr int kRuns = 40;
  for (int run = 0; run < kRuns; ++run) {
    BetweennessOptions o = exact(2);
    o.sample_sources = 100;
    o.seed = static_cast<std::uint64_t>(run) + 1;
    const auto r = betweenness(g, o).scores;
    for (std::size_t i = 0; i < r.size(); ++i) {
      mean[i] += r[i] / kRuns;
    }
  }
  // Unbiased estimator: the mean over many seeds tracks the exact values.
  const double total_truth = std::accumulate(truth.begin(), truth.end(), 0.0);
  const double total_mean = std::accumulate(mean.begin(), mean.end(), 0.0);
  EXPECT_NEAR(total_mean / total_truth, 1.0, 0.05);
  const auto top = static_cast<std::size_t>(std::max_element(truth.begin(), truth.end()) - truth.begin());
  EXPECT_NEAR(mean[top] / truth[top], 1.0, 0.2);
}

}  // namespace
}  // namespace sg::graph
