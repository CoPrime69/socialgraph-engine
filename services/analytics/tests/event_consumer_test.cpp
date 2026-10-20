#include "event_consumer.hpp"
#include "fixtures.hpp"

#include <sg/common/redis.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <random>
#include <thread>

namespace sg::analytics {
namespace {

using namespace std::chrono_literals;

std::optional<sg::common::RedisOptions> test_redis() {
  const char* host = std::getenv("SG_TEST_REDIS_HOST");
  if (host == nullptr || *host == '\0') return std::nullopt;
  sg::common::RedisOptions options;
  options.host = host;
  if (const char* port = std::getenv("SG_TEST_REDIS_PORT")) options.port = std::atoi(port);
  return options;
}

template <class Predicate>
bool eventually(Predicate predicate, std::chrono::milliseconds timeout = 5s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(10ms);
  }
  return predicate();
}

class EventConsumerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto redis = test_redis();
    if (!redis) GTEST_SKIP() << "SG_TEST_REDIS_HOST not set";
    redis_options_ = *redis;
    stream_ = "test.graph.events." + std::to_string(std::random_device{}());
  }

  void publish(const sg::common::Event& event) {
    sg::common::RedisClient client(redis_options_);
    client.xadd(stream_, sg::common::to_stream_fields(event));
  }

  // Snapshot = the two-clique graph at version 10, then whatever extra edges the test sets.
  SnapshotFetcher fetcher() {
    return [this] {
      ++snapshot_calls_;
      ReadModel scratch;
      testing::load_two_cliques(scratch);
      auto state = scratch.materialize();
      Snapshot s{snapshot_version_.load(), state->users, state->edges};
      return s;
    };
  }

  sg::common::RedisOptions redis_options_;
  std::string stream_;
  std::atomic<int> snapshot_calls_{0};
  std::atomic<std::uint64_t> snapshot_version_{10};
};

TEST_F(EventConsumerTest, BootstrapsThenAppliesEvents) {
  publish(testing::connection_added(9, 1, 2));  // older than the snapshot: ignored
  publish(testing::connection_added(11, 9, 1));
  ReadModel model;
  AnalyticsEngine engine(model, {});
  EventConsumer consumer(model, engine, fetcher(), redis_options_, {stream_, true});
  consumer.start();
  ASSERT_TRUE(eventually([&] { return model.version() == 11; }));
  publish(testing::connection_removed(12, 1, 9));
  ASSERT_TRUE(eventually([&] { return model.version() == 12; }));
  EXPECT_EQ(model.materialize()->edges.size(), 13U);
  // The counter is bumped right after the model changes, so it may trail the version briefly.
  EXPECT_TRUE(eventually([&] { return consumer.events_applied() == 2U; })) << consumer.events_applied();
  EXPECT_EQ(snapshot_calls_, 1);
  consumer.stop();
}

TEST_F(EventConsumerTest, GapTriggersResync) {
  ReadModel model;
  AnalyticsEngine engine(model, {});
  EventConsumer consumer(model, engine, fetcher(), redis_options_, {stream_, false});
  consumer.start();
  ASSERT_TRUE(eventually([&] { return model.ready(); }));
  snapshot_version_ = 20;  // the producer moved on; versions 11..19 were "lost"
  publish(testing::connection_added(20, 9, 1));
  ASSERT_TRUE(eventually([&] { return model.version() == 20; }));
  EXPECT_GE(consumer.resyncs(), 1U);
  EXPECT_EQ(snapshot_calls_, 2);
  consumer.stop();
}

TEST_F(EventConsumerTest, RetriesUntilSnapshotAvailable) {
  ReadModel model;
  AnalyticsEngine engine(model, {});
  std::atomic<int> attempts{0};
  auto flaky = [&, inner = fetcher()]() -> Snapshot {
    if (++attempts < 3) throw std::runtime_error("graph-service unavailable");
    return inner();
  };
  EventConsumer consumer(model, engine, flaky, redis_options_, {stream_, false});
  consumer.start();
  ASSERT_TRUE(eventually([&] { return model.ready(); }));
  EXPECT_GE(attempts, 3);
  consumer.stop();
}

}  // namespace
}  // namespace sg::analytics
