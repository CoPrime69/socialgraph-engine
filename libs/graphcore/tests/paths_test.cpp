#include <gtest/gtest.h>

#include <stdexcept>
#include <thread>

#include "sg/graph/paths.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

/// Checks that `path` is a simple walk along existing edges from `from` to `to`.
void expect_valid_path(const CsrGraph& g, const std::vector<NodeId>& path, NodeId from, NodeId to) {
  ASSERT_FALSE(path.empty());
  EXPECT_EQ(path.front(), from);
  EXPECT_EQ(path.back(), to);
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    EXPECT_TRUE(g.has_edge(path[i], path[i + 1])) << path[i] << "-" << path[i + 1];
  }
  std::vector<NodeId> sorted = path;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end()) << "repeated node";
}

TEST(BfsDistances, PathAndDisconnected) {
  EXPECT_EQ(bfs_distances(testing::path_graph(4), 1), (std::vector<std::int32_t>{1, 0, 1, 2}));
  const CsrGraph g = testing::make_graph(5, {{0, 1}, {3, 4}});
  EXPECT_EQ(bfs_distances(g, 0), (std::vector<std::int32_t>{0, 1, -1, -1, -1}));
  EXPECT_EQ(bfs_distances(g, 2), (std::vector<std::int32_t>{-1, -1, 0, -1, -1}));
  EXPECT_THROW((void)bfs_distances(g, 5), std::out_of_range);
}

TEST(ShortestPath, SameNode) {
  const CsrGraph g = testing::path_graph(3);
  EXPECT_EQ(shortest_path(g, 1, 1), (std::vector<NodeId>{1}));
  // Even an isolated node reaches itself.
  EXPECT_EQ(shortest_path(CsrGraph::from_pairs(1, {}), 0, 0), (std::vector<NodeId>{0}));
}

TEST(ShortestPath, PathGraphBothDirections) {
  const CsrGraph g = testing::path_graph(6);
  EXPECT_EQ(shortest_path(g, 0, 5), (std::vector<NodeId>{0, 1, 2, 3, 4, 5}));
  EXPECT_EQ(shortest_path(g, 4, 1), (std::vector<NodeId>{4, 3, 2, 1}));
  EXPECT_EQ(shortest_path(g, 2, 3), (std::vector<NodeId>{2, 3}));
}

TEST(ShortestPath, CycleTakesTheShortWayRound) {
  const CsrGraph g = testing::cycle_graph(10);
  const auto p = shortest_path(g, 1, 8);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(*p, (std::vector<NodeId>{1, 0, 9, 8}));
}

TEST(ShortestPath, UnreachableIsNullopt) {
  const CsrGraph g = testing::make_graph(6, {{0, 1}, {1, 2}, {3, 4}});
  EXPECT_EQ(shortest_path(g, 0, 4), std::nullopt);
  EXPECT_EQ(shortest_path(g, 5, 0), std::nullopt);
  EXPECT_EQ(shortest_path(g, 0, 5), std::nullopt);
}

TEST(ShortestPath, OutOfRangeThrows) {
  const CsrGraph g = testing::path_graph(3);
  EXPECT_THROW((void)shortest_path(g, 0, 3), std::out_of_range);
  EXPECT_THROW((void)shortest_path(g, 3, 0), std::out_of_range);
  EXPECT_THROW((void)shortest_path(CsrGraph{}, 0, 0), std::out_of_range);
}

TEST(ShortestPath, TwoCliquesCrossTheBridge) {
  const CsrGraph g = testing::two_cliques_with_bridge(5);
  const auto p = shortest_path(g, 0, 9);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(*p, (std::vector<NodeId>{0, 4, 5, 9}));
}

TEST(ShortestPath, LengthEqualsBfsDistanceOnRandomGraphs) {
  // Property test over many seeds, mixing sparse (disconnected) and dense graphs.
  for (std::uint64_t seed = 1; seed <= 150; ++seed) {
    SplitMix64 rng(seed * 7919);
    const std::size_t n = 2 + static_cast<std::size_t>(rng.bounded(120));
    const double p = 0.5 / static_cast<double>(n) + rng.uniform01() * 4.0 / static_cast<double>(n);
    const CsrGraph g = testing::random_graph(n, p, seed);
    for (int query = 0; query < 25; ++query) {
      const auto from = static_cast<NodeId>(rng.bounded(n));
      const auto to = static_cast<NodeId>(rng.bounded(n));
      const auto distance = bfs_distances(g, from)[to];
      const auto path = shortest_path(g, from, to);
      if (distance < 0) {
        EXPECT_FALSE(path.has_value()) << "seed " << seed;
        continue;
      }
      ASSERT_TRUE(path.has_value()) << "seed " << seed << " " << from << "->" << to;
      EXPECT_EQ(path->size(), static_cast<std::size_t>(distance) + 1) << "seed " << seed;
      expect_valid_path(g, *path, from, to);
    }
  }
}

TEST(ShortestPath, BfsDistancesMatchFloydWarshall) {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const CsrGraph g = testing::random_graph(40, 0.06, seed);
    const auto all = testing::floyd_warshall(g);
    for (NodeId s = 0; s < 40; ++s) {
      const auto d = bfs_distances(g, s);
      for (NodeId t = 0; t < 40; ++t) {
        EXPECT_EQ(d[t], all[s][t]);
      }
    }
  }
}

TEST(ShortestPath, ScratchIsSafeAcrossGraphsOfDifferentSizesAndThreads) {
  // The per-thread scratch must not leak state between graphs or between threads.
  const CsrGraph big = testing::path_graph(500);
  const CsrGraph small = testing::cycle_graph(5);
  auto worker = [&] {
    for (int i = 0; i < 50; ++i) {
      ASSERT_EQ(shortest_path(big, 0, 499)->size(), 500U);
      ASSERT_EQ(shortest_path(small, 0, 2)->size(), 3U);
      ASSERT_EQ(shortest_path(big, 250, 251)->size(), 2U);
    }
  };
  std::thread a(worker);
  std::thread b(worker);
  worker();
  a.join();
  b.join();
}

TEST(ConnectedComponents, Basics) {
  const auto empty = connected_components(CsrGraph{});
  EXPECT_EQ(empty.count, 0U);
  EXPECT_EQ(empty.largest, 0U);

  const CsrGraph g = testing::make_graph(7, {{1, 2}, {4, 5}, {5, 6}, {0, 3}});
  const auto r = connected_components(g);
  EXPECT_EQ(r.component, (std::vector<std::uint32_t>{0, 1, 1, 0, 2, 2, 2}));
  EXPECT_EQ(r.count, 3U);
  EXPECT_EQ(r.largest, 3U);

  const auto isolated = connected_components(CsrGraph::from_pairs(3, {}));
  EXPECT_EQ(isolated.component, (std::vector<std::uint32_t>{0, 1, 2}));
  EXPECT_EQ(isolated.count, 3U);
  EXPECT_EQ(isolated.largest, 1U);
}

TEST(ConnectedComponents, AgreeWithReachability) {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const CsrGraph g = testing::random_graph(60, 0.025, seed);
    const auto r = connected_components(g);
    const auto all = testing::floyd_warshall(g);
    std::size_t largest = 0;
    for (NodeId s = 0; s < 60; ++s) {
      std::size_t reach = 0;
      for (NodeId t = 0; t < 60; ++t) {
        const bool connected = all[s][t] != testing::kUnreachable;
        EXPECT_EQ(r.component[s] == r.component[t], connected);
        reach += connected ? 1 : 0;
      }
      largest = std::max(largest, reach);
    }
    EXPECT_EQ(r.largest, largest);
  }
}

}  // namespace
}  // namespace sg::graph
