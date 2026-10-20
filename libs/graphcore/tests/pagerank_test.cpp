#include <gtest/gtest.h>

#include <numeric>
#include <stdexcept>

#include "sg/graph/centrality.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

double sum(const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0); }

TEST(PageRank, EmptyGraph) {
  const auto r = pagerank(CsrGraph{});
  EXPECT_TRUE(r.scores.empty());
  EXPECT_TRUE(r.converged);
  EXPECT_EQ(r.iterations, 0);
}

TEST(PageRank, SingleNodeHasAllTheMass) {
  const auto r = pagerank(CsrGraph::from_pairs(1, {}));
  ASSERT_EQ(r.scores.size(), 1U);
  EXPECT_DOUBLE_EQ(r.scores[0], 1.0);
  EXPECT_TRUE(r.converged);
}

TEST(PageRank, RegularGraphsAreUniform) {
  for (const CsrGraph& g : {testing::cycle_graph(7), testing::complete_graph(6)}) {
    const auto r = pagerank(g);
    ASSERT_TRUE(r.converged);
    const double expected = 1.0 / static_cast<double>(g.node_count());
    for (const double s : r.scores) {
      EXPECT_NEAR(s, expected, 1e-12);
    }
  }
}

TEST(PageRank, StarMatchesClosedForm) {
  // Centre c and 4 leaves l: c = 0.15/5 + 0.85·4l, l = 0.15/5 + 0.85·c/4  ⇒  c = 0.132/0.2775.
  const auto r = pagerank(testing::star_graph(5));
  ASSERT_TRUE(r.converged);
  const double centre = 0.132 / 0.2775;
  EXPECT_NEAR(r.scores[0], centre, 1e-10);
  for (NodeId leaf = 1; leaf < 5; ++leaf) {
    EXPECT_NEAR(r.scores[leaf], (1.0 - centre) / 4.0, 1e-10);
  }
}

TEST(PageRank, PathIsSymmetricAndInnerNodesRankHigher) {
  const auto r = pagerank(testing::path_graph(5));
  EXPECT_NEAR(r.scores[0], r.scores[4], 1e-12);
  EXPECT_NEAR(r.scores[1], r.scores[3], 1e-12);
  EXPECT_GT(r.scores[1], r.scores[0]);
  EXPECT_NEAR(sum(r.scores), 1.0, 1e-12);
}

TEST(PageRank, DanglingNodesSpreadTheirMassUniformly) {
  // Path 0-1-2 plus isolated nodes 3 and 4.
  const CsrGraph g = testing::make_graph(5, {{0, 1}, {1, 2}});
  const auto r = pagerank(g);
  const auto reference = testing::dense_pagerank(g, 0.85, 2000);
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_NEAR(r.scores[i], reference[i], 1e-10);
  }
  EXPECT_NEAR(r.scores[3], r.scores[4], 1e-15);
  EXPECT_NEAR(sum(r.scores), 1.0, 1e-12);
}

TEST(PageRank, MatchesDenseMatrixPowerIterationOnRandomGraphs) {
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    const std::size_t n = 5 + seed % 31;
    const double p = (seed % 3 == 0) ? 0.05 : 0.2;  // some graphs have isolated nodes
    const CsrGraph g = testing::random_graph(n, p, seed);
    for (const double damping : {0.85, 0.5}) {
      const auto r = pagerank(g, PageRankOptions{damping, 1e-13, 1000});
      ASSERT_TRUE(r.converged) << "seed " << seed;
      const auto reference = testing::dense_pagerank(g, damping, 3000);
      for (std::size_t i = 0; i < n; ++i) {
        EXPECT_NEAR(r.scores[i], reference[i], 1e-10) << "seed " << seed << " node " << i;
      }
      EXPECT_NEAR(sum(r.scores), 1.0, 1e-12);
    }
  }
}

TEST(PageRank, ReportsNonConvergenceWhenCappedAndStillSumsToOne) {
  const CsrGraph g = testing::random_graph(40, 0.1, 7);
  const auto r = pagerank(g, PageRankOptions{0.85, 1e-15, 3});
  EXPECT_FALSE(r.converged);
  EXPECT_EQ(r.iterations, 3);
  EXPECT_NEAR(sum(r.scores), 1.0, 1e-12);
}

TEST(PageRank, ConvergesWellWithinTheDefaultIterationBudget) {
  const CsrGraph g = testing::random_graph(200, 0.03, 11);
  const auto r = pagerank(g);
  EXPECT_TRUE(r.converged);
  EXPECT_LT(r.iterations, 200);
}

TEST(PageRank, ZeroDampingIsUniform) {
  const auto r = pagerank(testing::star_graph(6), PageRankOptions{0.0, 1e-12, 10});
  for (const double s : r.scores) {
    EXPECT_NEAR(s, 1.0 / 6.0, 1e-15);
  }
}

TEST(PageRank, RejectsInvalidOptions) {
  const CsrGraph g = testing::path_graph(3);
  EXPECT_THROW((void)pagerank(g, PageRankOptions{1.5, 1e-10, 10}), std::invalid_argument);
  EXPECT_THROW((void)pagerank(g, PageRankOptions{-0.1, 1e-10, 10}), std::invalid_argument);
  EXPECT_THROW((void)pagerank(g, PageRankOptions{0.85, -1.0, 10}), std::invalid_argument);
  EXPECT_THROW((void)pagerank(g, PageRankOptions{0.85, 1e-10, 0}), std::invalid_argument);
}

}  // namespace
}  // namespace sg::graph
