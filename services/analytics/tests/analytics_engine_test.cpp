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

}  // namespace
}  // namespace sg::analytics
