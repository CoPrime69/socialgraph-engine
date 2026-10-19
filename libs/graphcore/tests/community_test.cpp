#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <stdexcept>

#include "sg/graph/community.hpp"
#include "sg/graph/generator.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

/// Checks the renumbering contract: ids 0..k−1, sizes non-increasing, ties by smallest member.
void expect_canonical_ids(const CommunityResult& r) {
  const std::size_t k = r.community_count;
  std::vector<std::size_t> size(k, 0);
  std::vector<std::size_t> smallest(k, r.membership.size());
  for (std::size_t i = 0; i < r.membership.size(); ++i) {
    ASSERT_LT(r.membership[i], k);
    ++size[r.membership[i]];
    smallest[r.membership[i]] = std::min(smallest[r.membership[i]], i);
  }
  for (std::size_t c = 0; c + 1 < k; ++c) {
    EXPECT_GT(size[c], 0U);
    EXPECT_TRUE(size[c] > size[c + 1] || (size[c] == size[c + 1] && smallest[c] < smallest[c + 1]))
        << "community " << c;
  }
}

TEST(Modularity, HandComputedValues) {
  // Two K4 joined by a bridge: m = 13, each side L = 6, K = 13 → Q = 2·(6/13 − 1/4) = 11/26.
  const CsrGraph g = testing::two_cliques_with_bridge(4);
  const std::vector<std::uint32_t> halves = {0, 0, 0, 0, 1, 1, 1, 1};
  EXPECT_NEAR(modularity(g, halves), 11.0 / 26.0, 1e-15);
  // Everything in one community: Q = 1 − 1 = 0.
  EXPECT_NEAR(modularity(g, std::vector<std::uint32_t>(8, 5)), 0.0, 1e-15);
  // Resolution γ scales the null-model term: 12/13 − γ·1/2.
  EXPECT_NEAR(modularity(g, halves, 0.5), 12.0 / 13.0 - 0.25, 1e-15);
}

TEST(Modularity, ArbitraryLabelsAndSizeCheck) {
  const CsrGraph g = testing::two_cliques_with_bridge(4);
  const std::vector<std::uint32_t> big_labels = {4000000000U, 4000000000U, 4000000000U, 4000000000U, 7, 7, 7, 7};
  EXPECT_NEAR(modularity(g, big_labels), 11.0 / 26.0, 1e-15);
  EXPECT_THROW((void)modularity(g, std::vector<std::uint32_t>(3, 0)), std::invalid_argument);
  EXPECT_EQ(modularity(CsrGraph::from_pairs(3, {}), std::vector<std::uint32_t>{0, 1, 2}), 0.0);
}

TEST(Modularity, MatchesDenseFormulaOnRandomWeightedPartitions) {
  for (std::uint64_t seed = 1; seed <= 25; ++seed) {
    SplitMix64 rng(seed);
    const std::size_t n = 2 + static_cast<std::size_t>(rng.bounded(40));
    std::vector<WeightedEdge> edges;
    for (NodeId i = 0; i < n; ++i) {
      for (NodeId j = i + 1; j < n; ++j) {
        if (rng.uniform01() < 0.2) {
          edges.push_back({i, j, 0.5 + rng.uniform01()});
        }
      }
    }
    const CsrGraph g = CsrGraph::from_edges(n, edges);
    std::vector<std::uint32_t> membership(n);
    for (auto& c : membership) {
      c = static_cast<std::uint32_t>(rng.bounded(4));
    }
    for (const double gamma : {1.0, 0.7}) {
      EXPECT_NEAR(modularity(g, membership, gamma), testing::dense_modularity(g, membership, gamma), 1e-12);
    }
  }
}

TEST(Louvain, EmptyGraph) {
  const auto r = louvain(CsrGraph{});
  EXPECT_TRUE(r.membership.empty());
  EXPECT_EQ(r.community_count, 0U);
  EXPECT_EQ(r.modularity, 0.0);
  EXPECT_EQ(r.levels, 0);
}

TEST(Louvain, NoEdgesGivesSingletons) {
  const auto r = louvain(CsrGraph::from_pairs(4, {}));
  EXPECT_EQ(r.membership, (std::vector<std::uint32_t>{0, 1, 2, 3}));
  EXPECT_EQ(r.community_count, 4U);
  EXPECT_EQ(r.modularity, 0.0);
  EXPECT_EQ(r.levels, 0);
}

TEST(Louvain, SingleNodeAndSingleEdge) {
  const auto one = louvain(CsrGraph::from_pairs(1, {}));
  EXPECT_EQ(one.membership, std::vector<std::uint32_t>{0});
  const auto edge = louvain(testing::path_graph(2));
  EXPECT_EQ(edge.membership, (std::vector<std::uint32_t>{0, 0}));
  EXPECT_EQ(edge.community_count, 1U);
  EXPECT_NEAR(edge.modularity, 0.0, 1e-15);
}

TEST(Louvain, TwoCliquesWithBridge) {
  const auto r = louvain(testing::two_cliques_with_bridge(4));
  EXPECT_EQ(r.membership, (std::vector<std::uint32_t>{0, 0, 0, 0, 1, 1, 1, 1}));
  EXPECT_EQ(r.community_count, 2U);
  EXPECT_NEAR(r.modularity, 11.0 / 26.0, 1e-12);
  EXPECT_GE(r.levels, 1);
  expect_canonical_ids(r);
}

TEST(Louvain, RingOfCliquesFindsEveryClique) {
  for (const auto& [cliques, size] : {std::pair<std::size_t, std::size_t>{6, 5}, {10, 6}, {16, 8}}) {
    const CsrGraph g = testing::ring_of_cliques(cliques, size);
    const auto r = louvain(g);
    ASSERT_EQ(r.community_count, cliques);
    for (std::size_t c = 0; c < cliques; ++c) {
      for (std::size_t i = 1; i < size; ++i) {
        EXPECT_EQ(r.membership[c * size + i], r.membership[c * size]) << "clique " << c;
      }
      if (c > 0) {
        EXPECT_NE(r.membership[c * size], r.membership[(c - 1) * size]);
      }
    }
    std::vector<std::uint32_t> planted(cliques * size);
    for (std::size_t i = 0; i < planted.size(); ++i) {
      planted[i] = static_cast<std::uint32_t>(i / size);
    }
    EXPECT_NEAR(r.modularity, modularity(g, planted), 1e-12);
    expect_canonical_ids(r);
  }
}

TEST(Louvain, ReportedModularityMatchesIndependentComputation) {
  for (std::uint64_t seed = 1; seed <= 15; ++seed) {
    const CsrGraph g = testing::random_graph(30 + seed * 3, 0.1, seed);
    const auto r = louvain(g);
    EXPECT_NEAR(r.modularity, testing::dense_modularity(g, r.membership), 1e-12) << seed;
    EXPECT_GE(r.modularity, 0.0);
    expect_canonical_ids(r);
  }
}

TEST(Louvain, RecoversPlantedGroups) {
  // 12 groups of 25 nodes, μ = 0.1: almost all edges are inside groups.
  std::vector<std::uint32_t> groups(300);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>(i % 12);
  }
  GeneratorOptions gen;
  gen.average_degree = 12;
  gen.mixing = 0.1;
  gen.seed = 5;
  const auto edges = generate_homophilous_edges(groups, gen);
  const CsrGraph g = CsrGraph::from_pairs(groups.size(), edges);
  const auto r = louvain(g);
  // Purity: the share of nodes whose community's majority group is their own group.
  std::map<std::uint32_t, std::map<std::uint32_t, int>> counts;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    ++counts[r.membership[i]][groups[i]];
  }
  int majority = 0;
  for (const auto& [community, by_group] : counts) {
    int best = 0;
    for (const auto& [group, count] : by_group) {
      best = std::max(best, count);
    }
    majority += best;
  }
  EXPECT_GE(static_cast<double>(majority) / static_cast<double>(groups.size()), 0.95);
  EXPECT_GE(r.community_count, 10U);
  EXPECT_LE(r.community_count, 14U);
  EXPECT_GE(r.modularity, modularity(g, groups) - 1e-9);
}

TEST(Louvain, DeterministicAcrossRunsAndInputOrder) {
  std::vector<std::uint32_t> groups(400);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>((i * 7) % 15);
  }
  auto edges = generate_homophilous_edges(groups);
  const CsrGraph g = CsrGraph::from_pairs(groups.size(), edges);
  const auto first = louvain(g);
  for (int run = 0; run < 3; ++run) {
    const auto again = louvain(g);
    EXPECT_EQ(again.membership, first.membership);
    EXPECT_EQ(again.modularity, first.modularity);
    EXPECT_EQ(again.levels, first.levels);
  }
  // Same edges in a different order and orientation → same CSR → same communities.
  std::reverse(edges.begin(), edges.end());
  for (auto& e : edges) {
    std::swap(e.first, e.second);
  }
  EXPECT_EQ(louvain(CsrGraph::from_pairs(groups.size(), edges)).membership, first.membership);
}

TEST(Louvain, UsesEdgeWeights) {
  // A 4-cycle 0-1-2-3-0: heavy edges 0-1 and 2-3 decide the pairing.
  const std::vector<WeightedEdge> a = {{0, 1, 10}, {1, 2, 1}, {2, 3, 10}, {3, 0, 1}};
  const auto ra = louvain(CsrGraph::from_edges(4, a));
  EXPECT_EQ(ra.membership[0], ra.membership[1]);
  EXPECT_EQ(ra.membership[2], ra.membership[3]);
  EXPECT_NE(ra.membership[0], ra.membership[2]);
  const std::vector<WeightedEdge> b = {{0, 1, 1}, {1, 2, 10}, {2, 3, 1}, {3, 0, 10}};
  const auto rb = louvain(CsrGraph::from_edges(4, b));
  EXPECT_EQ(rb.membership[1], rb.membership[2]);
  EXPECT_EQ(rb.membership[3], rb.membership[0]);
  EXPECT_NE(rb.membership[0], rb.membership[1]);
}

TEST(Louvain, ResolutionControlsGranularity) {
  const CsrGraph g = testing::ring_of_cliques(8, 4);
  const auto coarse = louvain(g, LouvainOptions{0.05, 10, 50, 1e-7});
  const auto normal = louvain(g);
  const auto fine = louvain(g, LouvainOptions{20.0, 10, 50, 1e-7});
  EXPECT_LT(coarse.community_count, normal.community_count);
  EXPECT_GT(fine.community_count, normal.community_count);
}

TEST(Louvain, IsolatedNodesStaySingletons) {
  // Two triangles plus two isolated nodes.
  const CsrGraph g = testing::make_graph(8, {{0, 1}, {1, 2}, {0, 2}, {3, 4}, {4, 5}, {3, 5}});
  const auto r = louvain(g);
  EXPECT_EQ(r.membership, (std::vector<std::uint32_t>{0, 0, 0, 1, 1, 1, 2, 3}));
  EXPECT_EQ(r.community_count, 4U);
}

TEST(Louvain, AggregationPreservesModularity) {
  // Multi-level run on a planted graph: later levels are only kept when they improve modularity,
  // so the hierarchy can never end below its first level.
  std::vector<std::uint32_t> groups(240);
  for (std::size_t i = 0; i < groups.size(); ++i) {
    groups[i] = static_cast<std::uint32_t>(i / 6);  // many small groups → several levels
  }
  GeneratorOptions gen;
  gen.average_degree = 6;
  gen.mixing = 0.3;
  const CsrGraph g = CsrGraph::from_pairs(groups.size(), generate_homophilous_edges(groups, gen));
  const auto multi = louvain(g);
  const auto one_level = louvain(g, LouvainOptions{1.0, 1, 50, 1e-7});
  EXPECT_GE(multi.levels, 2);
  EXPECT_EQ(one_level.levels, 1);
  EXPECT_GE(multi.modularity, one_level.modularity - 1e-12);

  expect_canonical_ids(multi);
}

TEST(Louvain, FinalPartitionHasNoImprovingMerge) {
  // At the last level every community is one aggregated node that found no move with a positive
  // gain, i.e. merging any two adjacent communities cannot raise modularity. This pins down the
  // aggregation (self-loop weights, strengths) because levels >= 2 decide these merges.
  for (std::uint64_t seed = 1; seed <= 6; ++seed) {
    std::vector<std::uint32_t> groups(240);
    for (std::size_t i = 0; i < groups.size(); ++i) {
      groups[i] = static_cast<std::uint32_t>(i / 6);
    }
    GeneratorOptions gen;
    gen.average_degree = 6;
    gen.mixing = 0.3;
    gen.seed = seed;
    const CsrGraph g = CsrGraph::from_pairs(groups.size(), generate_homophilous_edges(groups, gen));
    const auto r = louvain(g);
    ASSERT_GE(r.levels, 2) << seed;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> adjacent;
    for (const WeightedEdge& e : g.edge_list()) {
      const auto a = r.membership[e.u];
      const auto b = r.membership[e.v];
      if (a != b) {
        adjacent.emplace_back(std::min(a, b), std::max(a, b));
      }
    }
    std::sort(adjacent.begin(), adjacent.end());
    adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
    ASSERT_FALSE(adjacent.empty());
    for (const auto& [a, b] : adjacent) {
      auto merged = r.membership;
      for (auto& c : merged) {
        c = c == b ? a : c;
      }
      EXPECT_LE(modularity(g, merged), r.modularity + 1e-9) << "seed " << seed << " merge " << a << "+" << b;
    }
  }
}

TEST(Louvain, ZeroGainMovesAreNotMade) {
  // Single edge with γ = 2: joining the neighbour gains exactly 0 (1 − 2·1·1/2), so both nodes
  // stay singletons (staying put wins ties); merging would give the same modularity, −1.
  const auto r = louvain(testing::path_graph(2), LouvainOptions{2.0, 10, 50, 1e-7});
  EXPECT_EQ(r.membership, (std::vector<std::uint32_t>{0, 1}));
  EXPECT_EQ(r.levels, 0);
  EXPECT_NEAR(r.modularity, -1.0, 1e-15);
}

TEST(Louvain, RejectsNegativeWeightsAndBadOptions) {
  const std::vector<WeightedEdge> negative = {{0, 1, -1.0}};
  EXPECT_THROW((void)louvain(CsrGraph::from_edges(2, negative)), std::invalid_argument);
  const CsrGraph g = testing::path_graph(3);
  EXPECT_THROW((void)louvain(g, LouvainOptions{-1.0, 10, 50, 1e-7}), std::invalid_argument);
  EXPECT_THROW((void)louvain(g, LouvainOptions{1.0, 0, 50, 1e-7}), std::invalid_argument);
  EXPECT_THROW((void)louvain(g, LouvainOptions{1.0, 10, 0, 1e-7}), std::invalid_argument);
  EXPECT_THROW((void)louvain(g, LouvainOptions{1.0, 10, 50, -1.0}), std::invalid_argument);
}

}  // namespace
}  // namespace sg::graph
