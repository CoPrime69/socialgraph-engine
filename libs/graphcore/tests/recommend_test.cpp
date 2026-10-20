#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <thread>

#include "sg/graph/recommend.hpp"
#include "test_support.hpp"

namespace sg::graph {
namespace {

// 0 knows 1 and 2; 1 knows 3 and 4; 2 knows 3; 4 knows 6; 5 is isolated.
// Candidates of 0: 3 (common 1, 2), 4 (common 1), 5, 6 (no common neighbour).
CsrGraph small_graph() { return testing::make_graph(7, {{0, 1}, {0, 2}, {1, 3}, {1, 4}, {2, 3}, {4, 6}}); }

std::vector<Profile> small_profiles() {
  return {
      {0, 0, 50.0},  // 0: the user
      {0, 0, 50.0},  // 1
      {1, 1, 50.0},  // 2
      {1, 1, 50.0},  // 3: nothing in common but influence → affinity 0.3
      {0, 0, 50.0},  // 4: identical → affinity 1
      {0, 1, 60.0},  // 5: same category, |Δ| = 10 → 0.4 + 0.27
      {2, 2, 0.0},   // 6: |Δ| = 50 → 0.15
  };
}

std::vector<NodeId> nodes_of(const std::vector<Recommendation>& recs) {
  std::vector<NodeId> out;
  for (const auto& r : recs) {
    out.push_back(r.node);
  }
  return out;
}

TEST(Recommend, HandComputedScoresAndExplanations) {
  const CsrGraph g = small_graph();
  const auto profiles = small_profiles();
  const std::vector<std::uint32_t> membership = {0, 0, 0, 1, 0, 1, 1};
  const auto recs = recommend(g, profiles, membership, 0);
  ASSERT_EQ(recs.size(), 4U);

  const double aa3 = 1.0 / std::log(3.0) + 1.0 / std::log(2.0);  // via 1 (deg 3) and 2 (deg 2)
  const double aa4 = 1.0 / std::log(3.0);
  const double s3 = 0.55 * 1.0 + 0.35 * 0.3 + 0.0;
  const double s4 = 0.55 * aa4 / aa3 + 0.35 * 1.0 + 0.10;
  const double s5 = 0.35 * (0.4 + 0.3 * 0.9);
  const double s6 = 0.35 * 0.15;
  EXPECT_EQ(nodes_of(recs), (std::vector<NodeId>{4, 3, 5, 6}));  // s4 > s3 > s5 > s6

  EXPECT_NEAR(recs[0].score, s4, 1e-12);
  EXPECT_EQ(recs[0].mutual, 1U);
  EXPECT_NEAR(recs[0].adamic_adar, aa4, 1e-12);
  EXPECT_NEAR(recs[0].affinity, 1.0, 1e-12);
  EXPECT_TRUE(recs[0].same_category);
  EXPECT_TRUE(recs[0].same_branch);
  EXPECT_TRUE(recs[0].same_community);

  EXPECT_NEAR(recs[1].score, s3, 1e-12);
  EXPECT_EQ(recs[1].mutual, 2U);
  EXPECT_NEAR(recs[1].adamic_adar, aa3, 1e-12);
  EXPECT_FALSE(recs[1].same_category);
  EXPECT_FALSE(recs[1].same_community);

  EXPECT_NEAR(recs[2].score, s5, 1e-12);
  EXPECT_EQ(recs[2].mutual, 0U);
  EXPECT_EQ(recs[2].adamic_adar, 0.0);
  EXPECT_TRUE(recs[2].same_category);
  EXPECT_FALSE(recs[2].same_branch);

  EXPECT_NEAR(recs[3].score, s6, 1e-12);
}

TEST(Recommend, WithoutMembershipThereIsNoCommunityTerm) {
  const auto recs = recommend(small_graph(), small_profiles(), {}, 0);
  EXPECT_EQ(nodes_of(recs), (std::vector<NodeId>{3, 4, 5, 6}));  // 0.655 > 0.5628
  for (const auto& r : recs) {
    EXPECT_FALSE(r.same_community);
  }
}

TEST(Recommend, ExcludesSelfAndNeighbours) {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const CsrGraph g = testing::random_graph(80, 0.08, seed);
    std::vector<Profile> profiles(80);
    SplitMix64 rng(seed);
    for (auto& p : profiles) {
      p = Profile{static_cast<std::uint16_t>(rng.bounded(3)), static_cast<std::uint16_t>(rng.bounded(4)),
                  static_cast<double>(rng.bounded(1001)) / 10.0};
    }
    for (NodeId user = 0; user < 80; user += 7) {
      RecommendOptions opts;
      opts.limit = 1000;
      const auto recs = recommend(g, profiles, {}, user, opts);
      EXPECT_EQ(recs.size(), 80 - 1 - g.degree(user));
      std::set<NodeId> seen;
      for (const auto& r : recs) {
        EXPECT_NE(r.node, user);
        EXPECT_FALSE(g.has_edge(user, r.node));
        EXPECT_TRUE(seen.insert(r.node).second);
      }
    }
  }
}

TEST(Recommend, MatchesBruteForceRanking) {
  for (std::uint64_t seed = 1; seed <= 25; ++seed) {
    const std::size_t n = 30 + seed;
    const CsrGraph g = testing::random_graph(n, 0.1, seed);
    SplitMix64 rng(seed + 1000);
    std::vector<Profile> profiles(n);
    std::vector<std::uint32_t> membership(n);
    for (std::size_t i = 0; i < n; ++i) {
      profiles[i] = Profile{static_cast<std::uint16_t>(rng.bounded(3)), static_cast<std::uint16_t>(rng.bounded(3)),
                            static_cast<double>(rng.bounded(101))};
      membership[i] = static_cast<std::uint32_t>(rng.bounded(4));
    }
    const auto user = static_cast<NodeId>(rng.bounded(n));
    // Brute force: common neighbours by set intersection, full sort.
    struct Row {
      NodeId v;
      double score;
    };
    std::vector<Row> rows;
    double max_aa = 0.0;
    std::vector<double> aa(n, 0.0);
    for (NodeId v = 0; v < n; ++v) {
      if (v == user || g.has_edge(user, v)) {
        continue;
      }
      for (const NodeId z : g.neighbors(user)) {
        if (g.has_edge(z, v)) {
          aa[v] += 1.0 / std::log(static_cast<double>(g.degree(z)));
        }
      }
      max_aa = std::max(max_aa, aa[v]);
    }
    for (NodeId v = 0; v < n; ++v) {
      if (v == user || g.has_edge(user, v)) {
        continue;
      }
      const double structural = max_aa > 0 ? aa[v] / max_aa : 0.0;
      const double same = membership[v] == membership[user] ? 1.0 : 0.0;
      rows.push_back({v, 0.55 * structural + 0.35 * affinity(profiles[user], profiles[v]) + 0.10 * same});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
      return a.score > b.score || (a.score == b.score && a.v < b.v);
    });
    RecommendOptions opts;
    opts.limit = 8;
    const auto recs = recommend(g, profiles, membership, user, opts);
    ASSERT_EQ(recs.size(), std::min<std::size_t>(8, rows.size()));
    for (std::size_t i = 0; i < recs.size(); ++i) {
      EXPECT_EQ(recs[i].node, rows[i].v) << "seed " << seed << " rank " << i;
      EXPECT_NEAR(recs[i].score, rows[i].score, 1e-12);
    }
  }
}

TEST(Recommend, TiesAreBrokenBySmallerId) {
  // Isolated user, identical candidates: every score is equal.
  const CsrGraph g = CsrGraph::from_pairs(6, {});
  const std::vector<Profile> same(6, Profile{1, 1, 40.0});
  RecommendOptions opts;
  opts.limit = 3;
  EXPECT_EQ(nodes_of(recommend(g, same, {}, 2, opts)), (std::vector<NodeId>{0, 1, 3}));
  // Two winners with equal scores ahead of the rest keep id order as well.
  std::vector<Profile> profiles(6, Profile{0, 0, 0.0});
  profiles[2] = Profile{1, 1, 40.0};
  profiles[5] = Profile{1, 1, 40.0};
  profiles[4] = Profile{1, 1, 40.0};  // the user
  EXPECT_EQ(nodes_of(recommend(g, profiles, {}, 4, opts)), (std::vector<NodeId>{2, 5, 0}));
}

TEST(Recommend, NewUserFallsBackToAffinity) {
  // Node 5 is a new user without connections; the others form a connected community.
  const CsrGraph g = testing::make_graph(6, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}});
  const std::vector<Profile> profiles = {
      {1, 2, 10.0},  // 0: different category & branch
      {0, 1, 75.0},  // 1: same category
      {0, 0, 20.0},  // 2: same category & branch, far influence
      {0, 0, 79.0},  // 3: same category & branch, close influence
      {3, 0, 80.0},  // 4: same branch only
      {0, 0, 80.0},  // 5: the new user
  };
  const auto recs = recommend(g, profiles, {}, 5);
  ASSERT_EQ(recs.size(), 5U);
  EXPECT_EQ(nodes_of(recs), (std::vector<NodeId>{3, 2, 1, 4, 0}));
  for (const auto& r : recs) {
    EXPECT_EQ(r.mutual, 0U);
    EXPECT_EQ(r.adamic_adar, 0.0);
    EXPECT_NEAR(r.score, 0.35 * r.affinity, 1e-15);
  }
}

TEST(Recommend, LimitAndCustomWeights) {
  const CsrGraph g = small_graph();
  const auto profiles = small_profiles();
  RecommendOptions opts;
  opts.limit = 2;
  EXPECT_EQ(recommend(g, profiles, {}, 0, opts).size(), 2U);
  opts.limit = 0;
  EXPECT_TRUE(recommend(g, profiles, {}, 0, opts).empty());
  // Structure only: Adamic-Adar decides (3 > 4 > the rest, then id order).
  opts = RecommendOptions{10, 1.0, 0.0, 0.0};
  EXPECT_EQ(nodes_of(recommend(g, profiles, {}, 0, opts)), (std::vector<NodeId>{3, 4, 5, 6}));
}

TEST(Recommend, ValidatesInput) {
  const CsrGraph g = small_graph();
  const auto profiles = small_profiles();
  EXPECT_THROW((void)recommend(g, std::span<const Profile>(profiles).first(3), {}, 0), std::invalid_argument);
  const std::vector<std::uint32_t> short_membership = {0, 1};
  EXPECT_THROW((void)recommend(g, profiles, short_membership, 0), std::invalid_argument);
  EXPECT_THROW((void)recommend(g, profiles, {}, 7), std::out_of_range);
  EXPECT_THROW((void)recommend(g, profiles, {}, 0, RecommendOptions{10, -1.0, 0.35, 0.1}), std::invalid_argument);
}

struct Instance {
  CsrGraph graph;
  std::vector<Profile> profiles;
  std::vector<std::uint32_t> membership;
};

Instance random_instance(std::size_t n, double p, std::uint64_t seed) {
  Instance out{testing::random_graph(n, p, seed), std::vector<Profile>(n), std::vector<std::uint32_t>(n)};
  SplitMix64 rng(seed * 7919);
  for (std::size_t i = 0; i < n; ++i) {
    out.profiles[i] = Profile{static_cast<std::uint16_t>(rng.bounded(4)), static_cast<std::uint16_t>(rng.bounded(5)),
                              static_cast<double>(rng.bounded(1001)) / 10.0};
    out.membership[i] = static_cast<std::uint32_t>(rng.bounded(6));
  }
  return out;
}

void expect_same(const std::vector<Recommendation>& actual, const std::vector<Recommendation>& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < actual.size(); ++i) {
    EXPECT_EQ(actual[i].node, expected[i].node) << "rank " << i;
    EXPECT_EQ(actual[i].score, expected[i].score) << "rank " << i;
    EXPECT_EQ(actual[i].mutual, expected[i].mutual) << "rank " << i;
    EXPECT_EQ(actual[i].adamic_adar, expected[i].adamic_adar) << "rank " << i;
  }
}

/// The result of a query run on a brand-new thread, i.e. with a never-used scratch area.
std::vector<Recommendation> on_fresh_thread(const Instance& in, NodeId user, const RecommendOptions& opts) {
  std::vector<Recommendation> out;
  std::thread([&] { out = recommend(in.graph, in.profiles, in.membership, user, opts); }).join();
  return out;
}

TEST(Recommend, ReusedScratchGivesTheSameResultsAsAFreshOne) {
  // Alternate between a larger and a smaller graph (the scratch keeps the larger size and must
  // be all-zero for every query), with failing calls in between.
  const Instance large = random_instance(150, 0.06, 3);
  const Instance small = random_instance(40, 0.2, 4);
  RecommendOptions opts;
  opts.limit = 15;
  for (int round = 0; round < 3; ++round) {
    for (const Instance* in : {&large, &small}) {
      const auto n = static_cast<NodeId>(in->graph.node_count());
      for (NodeId user = static_cast<NodeId>(round); user < n; user += 9) {
        expect_same(recommend(in->graph, in->profiles, in->membership, user, opts), on_fresh_thread(*in, user, opts));
      }
      EXPECT_THROW((void)recommend(in->graph, in->profiles, in->membership, n, opts), std::out_of_range);
    }
  }
}

TEST(Recommend, ConcurrentQueriesMatchSequentialOnes) {
  const Instance in = random_instance(300, 0.03, 11);
  const auto n = static_cast<NodeId>(in.graph.node_count());
  std::vector<std::vector<Recommendation>> sequential(n);
  for (NodeId user = 0; user < n; ++user) {
    sequential[user] = recommend(in.graph, in.profiles, in.membership, user);
  }
  std::vector<std::vector<Recommendation>> concurrent(n);
  parallel_for(0, n, 4, [&](std::size_t user) {
    concurrent[user] = recommend(in.graph, in.profiles, in.membership, static_cast<NodeId>(user));
  });
  for (NodeId user = 0; user < n; ++user) {
    expect_same(concurrent[user], sequential[user]);
  }
}

}  // namespace
}  // namespace sg::graph
