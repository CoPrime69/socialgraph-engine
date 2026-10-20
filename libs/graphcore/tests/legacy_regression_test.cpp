// Regression tests for the defects of the original implementation (docs/audit.md §4) that
// graphcore fixes. Each test first reproduces the legacy behaviour (the relevant logic of
// legacy/cpp files/connection_manager.cpp, re-implemented here in miniature) so the defect stays
// documented, then shows that the library behaves correctly on the same input.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <thread>
#include <unordered_map>

#include "sg/graph/graphcore.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

using LegacyAdjacency = std::unordered_map<std::string, std::vector<std::string>>;

std::string legacy_id(std::size_t i) { return "user" + std::to_string(i + 1); }

LegacyAdjacency to_legacy(const CsrGraph& g) {
  LegacyAdjacency adjacency;
  for (NodeId u = 0; u < g.node_count(); ++u) {
    auto& list = adjacency[legacy_id(u)];
    for (const NodeId v : g.neighbors(u)) {
      list.push_back(legacy_id(v));
    }
  }
  return adjacency;
}

/// The seed graph of contracts.md §8 (original 100 users, deterministic generator).
CsrGraph seed_graph() {
  const auto groups = testing::seed_groups(testing::seed_users());
  return CsrGraph::from_pairs(groups.size(), generate_homophilous_edges(groups));
}

// ---------------------------------------------------------------------------------------------
// Audit §4.1: closeness centrality was always infinite.
// ---------------------------------------------------------------------------------------------

/// Legacy GraphTraversal::bfsDistance: `distances[to]` default-inserts 0.0 before the comparison,
/// so no distance is ever updated.
std::unordered_map<std::string, double> legacy_bfs_distance(const LegacyAdjacency& graph,
                                                            const std::string& start) {
  std::unordered_map<std::string, double> distances;
  std::unordered_map<std::string, bool> visited;
  std::queue<std::string> queue;
  visited[start] = true;
  queue.push(start);
  while (!queue.empty()) {
    const std::string current = queue.front();
    queue.pop();
    if (distances.find(current) == distances.end()) {
      distances[current] = current == start ? 0.0 : std::numeric_limits<double>::max();
    }
    if (graph.find(current) != graph.end()) {
      for (const auto& neighbor : graph.at(current)) {
        const double new_dist = distances[current] + 1;
        if (new_dist < distances[neighbor]) {
          distances[neighbor] = new_dist;
        }
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          queue.push(neighbor);
        }
      }
    }
  }
  return distances;
}

double legacy_closeness(const LegacyAdjacency& graph, const std::string& id) {
  double total_distance = 0.0;
  int reachable_nodes = 0;
  for (const auto& [node, dist] : legacy_bfs_distance(graph, id)) {
    if (dist != std::numeric_limits<double>::max()) {
      total_distance += dist;
      ++reachable_nodes;
    }
  }
  return reachable_nodes > 1 ? (reachable_nodes - 1) / total_distance : 0.0;
}

TEST(LegacyRegression, Audit4_1_ClosenessIsFiniteAndCorrect) {
  const CsrGraph g = seed_graph();
  const LegacyAdjacency legacy = to_legacy(g);
  // The defect: every connected node scored +inf.
  EXPECT_TRUE(std::isinf(legacy_closeness(legacy, legacy_id(0))));
  EXPECT_TRUE(std::isinf(legacy_closeness(legacy, legacy_id(57))));

  const auto ours = closeness(g);
  const auto reference = testing::reference_closeness(g);
  for (NodeId u = 0; u < g.node_count(); ++u) {
    ASSERT_TRUE(std::isfinite(ours[u]));
    EXPECT_GT(ours[u], 0.0);
    EXPECT_NEAR(ours[u], reference[u], 1e-12);
  }
}

// ---------------------------------------------------------------------------------------------
// Audit §4.2: establishConnections created duplicate edges (a multigraph).
// ---------------------------------------------------------------------------------------------

TEST(LegacyRegression, Audit4_2_DuplicateEdgesAreMergedNotDoubleCounted) {
  // Legacy establishConnections visits every ordered pair (i, j) and (j, i) and appends to both
  // lists on success, so a pair drawn twice is stored twice.
  const auto users = testing::seed_users();
  const auto profiles = testing::seed_profiles(users);
  const std::size_t n = users.size();
  SplitMix64 rng(20241109);  // deterministic stand-in for the legacy std::random_device draws
  std::vector<std::vector<NodeId>> legacy_lists(n);
  testing::Pairs pushes;
  for (NodeId i = 0; i < n; ++i) {
    for (NodeId j = 0; j < n; ++j) {
      if (i == j) {
        continue;
      }
      const bool same = profiles[i].category == profiles[j].category && profiles[i].branch == profiles[j].branch;
      if (rng.uniform01() < (same ? 0.5 : 0.2)) {
        legacy_lists[i].push_back(j);
        legacy_lists[j].push_back(i);
        pushes.emplace_back(i, j);
      }
    }
  }
  const CsrGraph g = CsrGraph::from_pairs(n, pushes);

  // The defect: list sizes (the legacy out-degrees) count duplicated pairs twice.
  std::size_t legacy_half_edges = 0;
  for (NodeId u = 0; u < n; ++u) {
    legacy_half_edges += legacy_lists[u].size();
    const std::set<NodeId> distinct(legacy_lists[u].begin(), legacy_lists[u].end());
    EXPECT_EQ(g.degree(u), distinct.size());  // ours: each neighbour once
  }
  EXPECT_GT(legacy_half_edges, 2 * g.edge_count());

  // The duplicates distort PageRank: power iteration on the legacy multigraph...
  std::vector<double> legacy_rank(n, 1.0 / static_cast<double>(n));
  for (int it = 0; it < 300; ++it) {
    std::vector<double> next(n, 0.15 / static_cast<double>(n));
    for (NodeId u = 0; u < n; ++u) {
      for (const NodeId v : legacy_lists[u]) {
        next[v] += 0.85 * legacy_rank[u] / static_cast<double>(legacy_lists[u].size());
      }
    }
    legacy_rank = next;
  }
  // ...differs from PageRank of the actual (simple) graph, which ours matches exactly.
  const auto ours = pagerank(g).scores;
  const auto reference = testing::dense_pagerank(g, 0.85, 2000);
  double max_distortion = 0.0;
  for (NodeId u = 0; u < n; ++u) {
    EXPECT_NEAR(ours[u], reference[u], 1e-10);
    max_distortion = std::max(max_distortion, std::abs(legacy_rank[u] - ours[u]) / ours[u]);
  }
  EXPECT_GT(max_distortion, 0.01);

  // Feeding a duplicated edge list yields the same graph and the same results as the clean one.
  testing::Pairs clean;
  for (const WeightedEdge& e : g.edge_list()) {
    clean.emplace_back(e.u, e.v);
  }
  const CsrGraph from_clean = CsrGraph::from_pairs(n, clean);
  EXPECT_EQ(from_clean, g);
  BetweennessOptions one_thread;
  one_thread.threads = 1;
  EXPECT_EQ(betweenness(from_clean, one_thread).scores, betweenness(g, one_thread).scores);
}

// ---------------------------------------------------------------------------------------------
// Found while porting (not in the audit): the legacy "Brandes" ignores path counts σ. It splits a
// node's dependency evenly between its predecessors ((1 + δ_w) / |pred(w)|) instead of in
// proportion σ_v / σ_w, and never halves the undirected double count.
// ---------------------------------------------------------------------------------------------

std::vector<double> legacy_betweenness(const CsrGraph& g) {
  const std::size_t n = g.node_count();
  std::vector<double> result(n, 0.0);
  for (NodeId s = 0; s < n; ++s) {
    std::vector<double> distance(n, std::numeric_limits<double>::max());
    std::vector<std::vector<NodeId>> predecessors(n);
    std::vector<double> dependency(n, 0.0);
    std::stack<NodeId> stack;
    std::queue<NodeId> queue;
    distance[s] = 0;
    queue.push(s);
    while (!queue.empty()) {
      const NodeId curr = queue.front();
      queue.pop();
      stack.push(curr);
      for (const NodeId nb : g.neighbors(curr)) {
        if (distance[nb] == std::numeric_limits<double>::max()) {
          queue.push(nb);
          distance[nb] = distance[curr] + 1;
        }
        if (distance[nb] == distance[curr] + 1) {
          predecessors[nb].push_back(curr);
        }
      }
    }
    while (!stack.empty()) {
      const NodeId w = stack.top();
      stack.pop();
      for (const NodeId v : predecessors[w]) {
        dependency[v] += (1.0 + dependency[w]) / static_cast<double>(predecessors[w].size());
      }
      if (w != s) {
        result[w] += dependency[w];
      }
    }
  }
  return result;
}

TEST(LegacyRegression, BetweennessWeighsPredecessorsByPathCounts) {
  // 0 → {1,2,3}; 1,2 → 4 (σ = 2); 3 → 5 (σ = 1); 4,5 → 6 (σ = 3): node 4 carries 2/3 of the
  // shortest 0-6 paths, not 1/2.
  const CsrGraph g =
      testing::make_graph(7, {{0, 1}, {0, 2}, {0, 3}, {1, 4}, {2, 4}, {3, 5}, {4, 6}, {5, 6}});
  BetweennessOptions raw;
  raw.normalized = false;
  raw.threads = 1;
  const auto ours = betweenness(g, raw).scores;
  const auto truth = testing::brute_force_betweenness(g, false);
  const auto legacy = legacy_betweenness(g);
  for (NodeId v = 0; v < 7; ++v) {
    EXPECT_NEAR(ours[v], truth[v], 1e-12);
  }
  // Even after halving, the legacy values are wrong for nodes 4 (off by 1/12) and 5 (by 1/6).
  EXPECT_NEAR(legacy[4] / 2.0 - truth[4], -1.0 / 12.0, 1e-12);
  EXPECT_NEAR(legacy[5] / 2.0 - truth[5], 1.0 / 6.0, 1e-12);
}

// ---------------------------------------------------------------------------------------------
// Audit §4.3: communities were random attribute chunks, different on every run.
// ---------------------------------------------------------------------------------------------

TEST(LegacyRegression, Audit4_3_CommunitiesAreGraphBasedAndDeterministic) {
  const CsrGraph g = seed_graph();
  const auto first = louvain(g);
  for (int run = 0; run < 5; ++run) {
    const auto again = louvain(g);
    EXPECT_EQ(again.membership, first.membership);
    EXPECT_EQ(again.modularity, first.modularity);
  }
  // A measurable, independently verifiable quality instead of none.
  EXPECT_NEAR(first.modularity, testing::dense_modularity(g, first.membership), 1e-12);
  EXPECT_GT(first.modularity, 0.3);
  // Graph-based: the communities follow the edges. Most edges stay inside a community.
  std::size_t inside = 0;
  for (const WeightedEdge& e : g.edge_list()) {
    inside += first.membership[e.u] == first.membership[e.v] ? 1 : 0;
  }
  EXPECT_GT(static_cast<double>(inside) / static_cast<double>(g.edge_count()), 0.6);
  // Same edges presented in another order → same communities.
  auto edges = generate_homophilous_edges(testing::seed_groups(testing::seed_users()));
  std::reverse(edges.begin(), edges.end());
  EXPECT_EQ(louvain(CsrGraph::from_pairs(g.node_count(), edges)).membership, first.membership);
}

// ---------------------------------------------------------------------------------------------
// Audit §4.4 / §3: the recommendation filter was a no-op and the scoring had dead terms.
// ---------------------------------------------------------------------------------------------

TEST(LegacyRegression, Audit4_4_RecommendationsUseTheUsersAttributes) {
  // A new Engineering / software user (user1's profile) without connections joins the seed
  // network. Legacy printRecommendedConnections tested `pair.first` (the user itself), so every
  // candidate passed and Sales or Finance users were suggested.
  const auto users = testing::seed_users();
  auto profiles = testing::seed_profiles(users);
  const Profile newcomer = profiles[0];
  profiles.push_back(newcomer);
  auto edges = generate_homophilous_edges(testing::seed_groups(users));
  const CsrGraph g = CsrGraph::from_pairs(profiles.size(), edges);
  const auto user = static_cast<NodeId>(profiles.size() - 1);
  ASSERT_EQ(g.degree(user), 0U);

  const auto recs = recommend(g, profiles, {}, user);
  ASSERT_EQ(recs.size(), 10U);
  for (const auto& r : recs) {
    EXPECT_TRUE(r.same_category && r.same_branch) << users[r.node].handle << " is " << users[r.node].category;
    EXPECT_EQ(users[r.node].category, "Engineering");
    EXPECT_EQ(users[r.node].branch, "software");
  }
}

TEST(LegacyRegression, Audit4_4_CommunityBonusAndInfluenceSimilarityAreLive) {
  // Legacy excluded same-community candidates and then added a same-community bonus that could
  // never apply; its influence similarity 1 − min(|Δ|, 1) was 0 for any gap ≥ 1 on the 0-100 scale.
  const CsrGraph g = CsrGraph::from_pairs(4, {});
  const std::vector<Profile> profiles = {
      {0, 0, 50.0},  // user
      {1, 1, 55.0},  // |Δ| = 5, other community
      {1, 1, 80.0},  // |Δ| = 30, other community
      {1, 1, 80.0},  // |Δ| = 30, same community as the user
  };
  const std::vector<std::uint32_t> membership = {7, 1, 1, 7};
  const auto recs = recommend(g, profiles, membership, 0);
  ASSERT_EQ(recs.size(), 3U);
  // Live community bonus: node 3 outranks node 2, its otherwise identical twin, by exactly w_c.
  EXPECT_EQ(recs[0].node, 3U);
  EXPECT_TRUE(recs[0].same_community);
  EXPECT_EQ(recs[1].node, 1U);
  EXPECT_EQ(recs[2].node, 2U);
  EXPECT_NEAR(recs[0].score - recs[2].score, 0.10, 1e-12);
  // Graded influence: a 5-point gap beats a 30-point gap by 0.3·0.25 in affinity.
  EXPECT_NEAR(recs[1].affinity - recs[2].affinity, 0.3 * 0.25, 1e-12);
}

// ---------------------------------------------------------------------------------------------
// Audit §4.5 / §3: connection weights were random (mean 13.5 ± noise) with inconsistent
// thresholds; strengths are now a pure function of the two profiles.
// ---------------------------------------------------------------------------------------------

TEST(LegacyRegression, Audit4_5_ConnectionStrengthIsAPureBoundedFunction) {
  const auto profiles = testing::seed_profiles(testing::seed_users());
  for (std::size_t i = 0; i < profiles.size(); ++i) {
    for (std::size_t j = 0; j < profiles.size(); j += 9) {
      const double s = connection_strength(profiles[i], profiles[j]);
      EXPECT_EQ(s, connection_strength(profiles[j], profiles[i]));
      EXPECT_EQ(s, connection_strength(profiles[i], profiles[j]));
      EXPECT_GE(s, 0.0);
      EXPECT_LE(s, 1.0);
      EXPECT_EQ(s, std::round(s * 1000.0) / 1000.0);
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Audit §4.7: parallel centrality raced on shared hash maps (hidden only because OpenMP was
// never enabled). Here the parallel paths are exercised from several callers at once; the TSan
// build of this test is the actual race check.
// ---------------------------------------------------------------------------------------------

TEST(LegacyRegression, Audit4_7_ParallelCentralityIsRaceFreeAndDeterministic) {
  const CsrGraph g = testing::random_graph(400, 0.02, 4242);
  BetweennessOptions serial;
  serial.threads = 1;
  const auto expected_betweenness = betweenness(g, serial).scores;
  const auto expected_closeness = closeness(g, 1);

  auto run = [&] {
    BetweennessOptions parallel;
    parallel.threads = 4;
    EXPECT_EQ(betweenness(g, parallel).scores, expected_betweenness);
    EXPECT_EQ(closeness(g, 4), expected_closeness);
  };
  std::thread other(run);
  run();
  other.join();
}

}  // namespace
}  // namespace sg::graph
