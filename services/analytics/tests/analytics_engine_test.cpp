#include "analytics_engine.hpp"
#include "fixtures.hpp"

#include <sg/common/errors.hpp>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace sg::analytics {
namespace {

using testing::connection_added;
using testing::load_two_cliques;

TEST(ComputeAnalytics, FindsTheTwoCliquesAndTheBridge) {
  ReadModel model;
  load_two_cliques(model);
  auto a = compute_analytics(model.materialize(), {});

  EXPECT_EQ(a->version(), 10U);
  EXPECT_EQ(a->graph.node_count(), 9U);
  EXPECT_EQ(a->graph.edge_count(), 13U);
  EXPECT_EQ(a->components.count, 2U);  // the cliques + the isolated user 9
  EXPECT_EQ(a->components.largest, 8U);

  const auto& m = a->communities.membership;
  const auto n1 = *a->node(1);
  const auto n4 = *a->node(4);
  const auto n5 = *a->node(5);
  const auto n8 = *a->node(8);
  EXPECT_EQ(m[n1], m[n4]);
  EXPECT_EQ(m[n5], m[n8]);
  EXPECT_NE(m[n1], m[n5]);
  EXPECT_GT(a->communities.modularity, 0.3);

  // The bridge endpoints carry all shortest paths between the cliques.
  const auto& order = a->order[static_cast<std::size_t>(Metric::betweenness)];
  EXPECT_EQ(a->user(order[0]).id, 4);  // tie with 5 broken by smaller id
  EXPECT_EQ(a->user(order[1]).id, 5);
  EXPECT_EQ(a->ranks[static_cast<std::size_t>(Metric::betweenness)][n4], 1U);
  EXPECT_FALSE(a->betweenness_approximate);
}

TEST(ComputeAnalytics, RanksArePermutations) {
  ReadModel model;
  load_two_cliques(model);
  auto a = compute_analytics(model.materialize(), {});
  for (const auto& ranks : a->ranks) {
    std::vector<bool> seen(ranks.size() + 1, false);
    for (auto r : ranks) {
      ASSERT_GE(r, 1U);
      ASSERT_LE(r, ranks.size());
      EXPECT_FALSE(seen[r]);
      seen[r] = true;
    }
  }
}

TEST(ComputeAnalytics, SamplesBetweennessAboveThreshold) {
  ReadModel model;
  load_two_cliques(model);
  EngineOptions options;
  options.exact_betweenness_max_nodes = 4;
  options.betweenness_samples = 3;
  auto a = compute_analytics(model.materialize(), options);
  EXPECT_TRUE(a->betweenness_approximate);
}

TEST(ComputeAnalytics, EmptyGraph) {
  ReadModel model;
  model.reset(1, {}, {});
  auto a = compute_analytics(model.materialize(), {});
  EXPECT_EQ(a->graph.node_count(), 0U);
  EXPECT_EQ(a->communities.community_count, 0U);
}

TEST(AnalyticsEngine, UnavailableBeforeBootstrap) {
  ReadModel model;
  AnalyticsEngine engine(model, {});
  try {
    engine.current();
    FAIL() << "expected ApiError";
  } catch (const sg::common::ApiError& e) {
    EXPECT_EQ(e.status(), 503);
  }
}

TEST(AnalyticsEngine, CachesPerVersion) {
  ReadModel model;
  load_two_cliques(model);
  AnalyticsEngine engine(model, {});
  auto first = engine.current();
  auto second = engine.current();
  EXPECT_EQ(first.get(), second.get());
  EXPECT_EQ(engine.stats().computations, 1U);

  ASSERT_EQ(model.apply(connection_added(11, 9, 1)), ApplyResult::applied);
  auto third = engine.current();
  EXPECT_NE(third.get(), first.get());
  EXPECT_EQ(third->version(), 11U);
  EXPECT_EQ(engine.stats().computations, 2U);
  EXPECT_EQ(first->version(), 10U);  // old snapshots stay valid for readers still holding them
}

TEST(AnalyticsEngine, ConcurrentReadersShareOneComputation) {
  ReadModel model;
  load_two_cliques(model);
  AnalyticsEngine engine(model, {});
  constexpr int kThreads = 16;
  std::vector<std::shared_ptr<const Analytics>> results(kThreads);
  {
    std::vector<std::jthread> threads;
    for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back([&, i] { results[static_cast<std::size_t>(i)] = engine.current(); });
    }
  }
  EXPECT_EQ(engine.stats().computations, 1U);
  for (const auto& r : results) EXPECT_EQ(r.get(), results.front().get());
}

TEST(AnalyticsEngine, ReadersAndWriterRaceSafely) {
  ReadModel model;
  load_two_cliques(model);
  AnalyticsEngine engine(model, {});
  std::atomic<bool> done{false};
  std::jthread writer([&] {
    for (std::uint64_t v = 11; v < 61; ++v) {
      const bool add = v % 2 == 1;
      model.apply(add ? connection_added(v, 9, 1) : testing::connection_removed(v, 9, 1));
    }
    done = true;
  });
  std::vector<std::jthread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&] {
      while (!done) {
        auto a = engine.current();
        // Internal consistency of whatever version we got.
        ASSERT_EQ(a->graph.edge_count(), a->state->edges.size());
      }
    });
  }
  writer.join();
  for (auto& r : readers) r.join();
  EXPECT_EQ(engine.current()->version(), 60U);
}

TEST(AnalyticsEngine, BackgroundRefreshServesStaleSnapshotThenCatchesUp) {
  ReadModel model;
  load_two_cliques(model);
  EngineOptions options;
  options.min_refresh_interval = std::chrono::milliseconds(50);
  AnalyticsEngine engine(model, options);
  auto first = engine.current();  // nothing published yet: computed on demand
  engine.start_background_refresh();

  model.apply(connection_added(11, 9, 1));
  // Readers are not blocked by the change: they get the published snapshot immediately.
  EXPECT_EQ(engine.current()->version(), 10U);
  EXPECT_GE(engine.stats().stale_reads, 1U);

  engine.notify_changed();
  auto fresh = engine.wait_for_version(11, std::chrono::seconds(5));
  EXPECT_EQ(fresh->version(), 11U);
  EXPECT_EQ(engine.current()->version(), 11U);
  EXPECT_EQ(first->version(), 10U);
}

TEST(AnalyticsEngine, BackgroundRefreshCoalescesBursts) {
  ReadModel model;
  load_two_cliques(model);
  EngineOptions options;
  options.min_refresh_interval = std::chrono::milliseconds(200);
  AnalyticsEngine engine(model, options);
  (void)engine.current();
  engine.start_background_refresh();
  const auto before = engine.stats().computations;
  for (std::uint64_t v = 11; v < 41; ++v) {
    model.apply(v % 2 == 1 ? connection_added(v, 9, 1) : testing::connection_removed(v, 9, 1));
    engine.notify_changed();
  }
  EXPECT_EQ(engine.wait_for_version(40, std::chrono::seconds(5))->version(), 40U);
  // 30 changes inside the refresh window: far fewer than 30 recomputations.
  EXPECT_LE(engine.stats().computations - before, 3U);
}

TEST(AnalyticsEngine, WaitForVersionTimesOutWithLatestSnapshot) {
  ReadModel model;
  load_two_cliques(model);
  AnalyticsEngine engine(model, {});
  (void)engine.current();
  engine.start_background_refresh();
  const auto started = std::chrono::steady_clock::now();
  auto latest = engine.wait_for_version(99, std::chrono::milliseconds(50));
  EXPECT_EQ(latest->version(), 10U);
  EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(50));
}

TEST(ComputeAnalytics, CarryOverKeepsGlobalMetricsAndUpdatesStructure) {
  ReadModel model;
  load_two_cliques(model);
  const auto full = compute_analytics(model.materialize(), {});
  ASSERT_TRUE(full->full);
  EXPECT_EQ(full->centrality_version, 10U);

  // Node 9 joins the first clique and a new member 10 attaches to 9.
  model.apply(connection_added(11, 9, 1, 0.9));
  model.apply(testing::user_created(12, testing::make_user(10, "Sales", "sales", 5.0)));
  model.apply(connection_added(13, 10, 9, 0.4));
  const auto carried = compute_analytics(model.materialize(), {}, full.get());
  EXPECT_FALSE(carried->full);
  EXPECT_EQ(carried->version(), 13U);
  EXPECT_EQ(carried->centrality_version, 10U) << "global metrics still describe version 10";
  EXPECT_EQ(carried->centrality_computed_at, full->centrality_computed_at);

  const auto old_1 = *full->node(1);
  const auto new_1 = *carried->node(1);
  EXPECT_EQ(carried->metric(Metric::pagerank)[new_1], full->metric(Metric::pagerank)[old_1]);
  EXPECT_EQ(carried->metric(Metric::betweenness)[new_1], full->metric(Metric::betweenness)[old_1]);
  const auto ten = *carried->node(10);
  EXPECT_EQ(carried->metric(Metric::pagerank)[ten], 0.0) << "new members score 0 until the next full run";
  // Structure is current: degree and components reflect the new edges.
  EXPECT_GT(carried->metric(Metric::degree)[*carried->node(9)], full->metric(Metric::degree)[*full->node(9)]);
  EXPECT_EQ(carried->graph.edge_count(), full->graph.edge_count() + 2);
  // Communities: carried labels stay, the newcomer joins its strongest neighbour's community.
  const auto& m = carried->communities.membership;
  EXPECT_EQ(m[new_1], m[*carried->node(2)]);
  EXPECT_EQ(m[ten], m[*carried->node(9)]);
  EXPECT_LE(carried->communities.community_count, full->communities.community_count + 1);
  for (auto c : m) EXPECT_LT(c, carried->communities.community_count);
}

TEST(AnalyticsEngine, FullRecomputationIsRateLimitedAndCatchesUpWithoutWrites) {
  ReadModel model;
  load_two_cliques(model);
  EngineOptions options;
  options.full_refresh_interval = std::chrono::milliseconds(300);
  AnalyticsEngine engine(model, options);
  ASSERT_TRUE(engine.current()->full);
  engine.start_background_refresh();
  const auto fulls = engine.stats().full_computations;

  model.apply(connection_added(11, 9, 1));
  engine.notify_changed();
  const auto quick = engine.wait_for_version(11, std::chrono::seconds(5));
  EXPECT_EQ(quick->version(), 11U);
  EXPECT_FALSE(quick->full) << "inside the interval only the structure is refreshed";
  EXPECT_EQ(engine.stats().full_computations, fulls);

  // With no further writes, the refresher still upgrades the snapshot once the interval passed.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!engine.current()->full && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  const auto upgraded = engine.current();
  EXPECT_TRUE(upgraded->full);
  EXPECT_EQ(upgraded->version(), 11U);
  EXPECT_EQ(upgraded->centrality_version, 11U);
  EXPECT_EQ(engine.stats().full_computations, fulls + 1);
}

TEST(RenderCache, StoresUntilFull) {
  RenderCache cache;
  EXPECT_EQ(cache.find("a"), nullptr);
  cache.insert("a", std::make_shared<const std::string>("{}"));
  ASSERT_NE(cache.find("a"), nullptr);
  EXPECT_EQ(*cache.find("a"), "{}");
  cache.insert("a", std::make_shared<const std::string>("[]"));  // first value wins
  EXPECT_EQ(*cache.find("a"), "{}");
}

}  // namespace
}  // namespace sg::analytics
