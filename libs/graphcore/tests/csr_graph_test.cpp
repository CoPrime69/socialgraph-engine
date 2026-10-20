#include <gtest/gtest.h>

#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include "sg/graph/csr_graph.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

using testing::Pairs;

std::vector<NodeId> to_vector(std::span<const NodeId> s) { return {s.begin(), s.end()}; }
std::vector<double> to_vector(std::span<const double> s) { return {s.begin(), s.end()}; }

TEST(CsrGraph, DefaultIsEmpty) {
  const CsrGraph g;
  EXPECT_EQ(g.node_count(), 0U);
  EXPECT_EQ(g.edge_count(), 0U);
  EXPECT_EQ(g.total_weight(), 0.0);
  EXPECT_EQ(g.max_degree(), 0U);
  EXPECT_TRUE(g.edge_list().empty());
  EXPECT_THROW((void)g.neighbors(0), std::out_of_range);
}

TEST(CsrGraph, ZeroNodesFromEmptyInputEqualsDefault) {
  EXPECT_EQ(CsrGraph::from_pairs(0, {}), CsrGraph{});
  EXPECT_EQ(CsrGraph::from_edges(0, {}), CsrGraph{});
}

TEST(CsrGraph, SingleNodeWithoutEdges) {
  const CsrGraph g = CsrGraph::from_pairs(1, {});
  EXPECT_EQ(g.node_count(), 1U);
  EXPECT_EQ(g.edge_count(), 0U);
  EXPECT_EQ(g.degree(0), 0U);
  EXPECT_TRUE(g.neighbors(0).empty());
  EXPECT_FALSE(g.has_edge(0, 0));
}

TEST(CsrGraph, BuildsSortedSymmetricAdjacency) {
  const Pairs edges = {{3, 1}, {0, 2}, {1, 0}, {2, 3}, {1, 2}};
  const CsrGraph g = CsrGraph::from_pairs(5, edges);
  EXPECT_EQ(g.node_count(), 5U);
  EXPECT_EQ(g.edge_count(), 5U);
  EXPECT_EQ(to_vector(g.neighbors(0)), (std::vector<NodeId>{1, 2}));
  EXPECT_EQ(to_vector(g.neighbors(1)), (std::vector<NodeId>{0, 2, 3}));
  EXPECT_EQ(to_vector(g.neighbors(2)), (std::vector<NodeId>{0, 1, 3}));
  EXPECT_EQ(to_vector(g.neighbors(3)), (std::vector<NodeId>{1, 2}));
  EXPECT_TRUE(g.neighbors(4).empty());
  EXPECT_EQ(g.degree(1), 3U);
  EXPECT_EQ(g.max_degree(), 3U);
  EXPECT_DOUBLE_EQ(g.total_weight(), 5.0);
  EXPECT_EQ(g.offsets().size(), 6U);
  EXPECT_EQ(g.adjacency().size(), 10U);
  EXPECT_EQ(g.adjacency_weights().size(), 10U);
}

TEST(CsrGraph, HasEdgeIsSymmetricAndUsesBinarySearch) {
  const CsrGraph g = testing::star_graph(6);
  for (NodeId leaf = 1; leaf < 6; ++leaf) {
    EXPECT_TRUE(g.has_edge(0, leaf));
    EXPECT_TRUE(g.has_edge(leaf, 0));
  }
  EXPECT_FALSE(g.has_edge(1, 2));
  EXPECT_FALSE(g.has_edge(3, 3));
  EXPECT_THROW((void)g.has_edge(0, 6), std::out_of_range);
  EXPECT_THROW((void)g.has_edge(6, 0), std::out_of_range);
}

TEST(CsrGraph, DropsSelfLoops) {
  const Pairs edges = {{0, 0}, {0, 1}, {1, 1}, {2, 2}};
  const CsrGraph g = CsrGraph::from_pairs(3, edges);
  EXPECT_EQ(g.edge_count(), 1U);
  EXPECT_FALSE(g.has_edge(0, 0));
  EXPECT_EQ(g.degree(2), 0U);
}

TEST(CsrGraph, MergesDuplicatesInBothOrientationsKeepingFirstWeight) {
  const std::vector<WeightedEdge> edges = {
      {0, 1, 0.25}, {1, 0, 0.75}, {0, 1, 0.5}, {1, 2, 2.0}, {2, 1, 3.0}, {0, 2, 1.5}};
  const CsrGraph g = CsrGraph::from_edges(3, edges);
  EXPECT_EQ(g.edge_count(), 3U);
  EXPECT_EQ(g.edge_weight(0, 1), 0.25);
  EXPECT_EQ(g.edge_weight(1, 0), 0.25);
  EXPECT_EQ(g.edge_weight(1, 2), 2.0);
  EXPECT_EQ(g.edge_weight(2, 1), 2.0);
  EXPECT_EQ(g.edge_weight(0, 2), 1.5);
  EXPECT_EQ(to_vector(g.weights(1)), (std::vector<double>{0.25, 2.0}));
  EXPECT_DOUBLE_EQ(g.total_weight(), 0.25 + 2.0 + 1.5);
}

TEST(CsrGraph, ReversedFirstOccurrenceStillWins) {
  const std::vector<WeightedEdge> edges = {{4, 2, 9.0}, {2, 4, 1.0}};
  const CsrGraph g = CsrGraph::from_edges(5, edges);
  EXPECT_EQ(g.edge_weight(2, 4), 9.0);
  EXPECT_EQ(g.edge_weight(4, 2), 9.0);
}

TEST(CsrGraph, OutOfRangeEndpointThrowsInvalidArgument) {
  const Pairs bad = {{0, 1}, {1, 3}};
  EXPECT_THROW((void)CsrGraph::from_pairs(3, bad), std::invalid_argument);
  const std::vector<WeightedEdge> bad_weighted = {{5, 0, 1.0}};
  EXPECT_THROW((void)CsrGraph::from_edges(5, bad_weighted), std::invalid_argument);
  const Pairs any = {{0, 0}};
  EXPECT_THROW((void)CsrGraph::from_pairs(0, any), std::invalid_argument);
}

TEST(CsrGraph, OutOfRangeSelfLoopStillThrows) {
  const Pairs bad = {{7, 7}};
  EXPECT_THROW((void)CsrGraph::from_pairs(3, bad), std::invalid_argument);
}

TEST(CsrGraph, NonFiniteWeightThrows) {
  const std::vector<WeightedEdge> nan_edge = {{0, 1, std::numeric_limits<double>::quiet_NaN()}};
  EXPECT_THROW((void)CsrGraph::from_edges(2, nan_edge), std::invalid_argument);
  const std::vector<WeightedEdge> inf_edge = {{0, 1, std::numeric_limits<double>::infinity()}};
  EXPECT_THROW((void)CsrGraph::from_edges(2, inf_edge), std::invalid_argument);
}

TEST(CsrGraph, AccessorsThrowOutOfRange) {
  const CsrGraph g = testing::path_graph(3);
  EXPECT_THROW((void)g.neighbors(3), std::out_of_range);
  EXPECT_THROW((void)g.weights(3), std::out_of_range);
  EXPECT_THROW((void)g.degree(3), std::out_of_range);
  EXPECT_THROW((void)g.edge_weight(0, 3), std::out_of_range);
  EXPECT_EQ(g.edge_weight(0, 2), std::nullopt);
}

TEST(CsrGraph, EdgeListIsCanonical) {
  const std::vector<WeightedEdge> edges = {{2, 0, 0.5}, {1, 3, 0.25}, {0, 1, 1.0}, {3, 2, 2.0}};
  const CsrGraph g = CsrGraph::from_edges(4, edges);
  const auto list = g.edge_list();
  ASSERT_EQ(list.size(), 4U);
  const std::vector<std::pair<NodeId, NodeId>> expected = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
  for (std::size_t i = 0; i < list.size(); ++i) {
    EXPECT_EQ(list[i].u, expected[i].first);
    EXPECT_EQ(list[i].v, expected[i].second);
  }
  EXPECT_EQ(list[1].w, 0.5);
  // Round trip.
  EXPECT_EQ(CsrGraph::from_edges(4, list), g);
}

TEST(CsrGraph, InputOrderDoesNotChangeTheGraph) {
  Pairs edges = {{0, 1}, {2, 3}, {1, 2}, {4, 0}, {3, 4}, {1, 3}};
  const CsrGraph a = CsrGraph::from_pairs(5, edges);
  std::reverse(edges.begin(), edges.end());
  for (auto& e : edges) {
    std::swap(e.first, e.second);
  }
  EXPECT_EQ(CsrGraph::from_pairs(5, edges), a);
}

TEST(CsrGraph, MapWeightsTransformsEveryWeight) {
  const std::vector<WeightedEdge> edges = {{0, 1, 0.2}, {1, 2, 0.6}};
  const CsrGraph g = CsrGraph::from_edges(3, edges);
  const CsrGraph mapped = g.map_weights(louvain_weight);
  EXPECT_DOUBLE_EQ(*mapped.edge_weight(0, 1), 0.6);
  EXPECT_DOUBLE_EQ(*mapped.edge_weight(2, 1), 0.8);
  EXPECT_DOUBLE_EQ(mapped.total_weight(), 1.4);
  EXPECT_EQ(to_vector(mapped.neighbors(1)), to_vector(g.neighbors(1)));
  EXPECT_THROW((void)g.map_weights([](double) { return std::numeric_limits<double>::infinity(); }),
               std::invalid_argument);
}

// Property test: random multigraph input against a std::map reference.
TEST(CsrGraph, MatchesSetReferenceOnRandomMultigraphs) {
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    SplitMix64 rng(seed);
    const std::size_t n = 1 + static_cast<std::size_t>(rng.bounded(60));
    const std::size_t m = static_cast<std::size_t>(rng.bounded(4 * n + 1));
    std::vector<WeightedEdge> edges;
    std::map<std::pair<NodeId, NodeId>, double> reference;  // first weight per unordered pair
    for (std::size_t i = 0; i < m; ++i) {
      const auto u = static_cast<NodeId>(rng.bounded(n));
      const auto v = static_cast<NodeId>(rng.bounded(n));
      const double w = static_cast<double>(rng.bounded(1000)) / 10.0;
      edges.push_back({u, v, w});
      if (u != v) {
        reference.try_emplace({std::min(u, v), std::max(u, v)}, w);
      }
    }
    const CsrGraph g = CsrGraph::from_edges(n, edges);
    ASSERT_EQ(g.edge_count(), reference.size()) << "seed " << seed;
    std::vector<std::set<NodeId>> adjacency(n);
    for (const auto& [pair, w] : reference) {
      adjacency[pair.first].insert(pair.second);
      adjacency[pair.second].insert(pair.first);
      EXPECT_EQ(g.edge_weight(pair.first, pair.second), w);
      EXPECT_EQ(g.edge_weight(pair.second, pair.first), w);
    }
    for (NodeId u = 0; u < n; ++u) {
      const std::vector<NodeId> expected(adjacency[u].begin(), adjacency[u].end());
      EXPECT_EQ(to_vector(g.neighbors(u)), expected) << "seed " << seed << " node " << u;
      for (NodeId v = 0; v < n; ++v) {
        EXPECT_EQ(g.has_edge(u, v), adjacency[u].count(v) == 1);
      }
    }
  }
}

}  // namespace
}  // namespace sg::graph
