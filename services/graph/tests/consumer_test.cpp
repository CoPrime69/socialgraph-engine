#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sg/common/errors.hpp>
#include <sg/common/events.hpp>
#include <sg/common/redis.hpp>
#include <sg/common/testing/test_env.hpp>

#include "pg_fixture.hpp"
#include "sg/graph_service/app/graph_service.hpp"
#include "sg/graph_service/infra/identity_events_consumer.hpp"

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using sg::graph_service::infra::IdentityEventsConsumer;

sg::common::Event registered(const std::string& event_id, std::int64_t user_id, const std::string& handle,
                             const std::string& branch = "javascript") {
  sg::common::Event e;
  e.event_id = event_id;
  e.type = "user.registered";
  e.occurred_at = "2026-10-19T20:00:00Z";
  e.payload = {{"user_id", user_id}, {"handle", handle},  {"name", "Member " + handle}, {"category", "Web Development"},
               {"branch", branch},   {"location", nullptr}, {"about", nullptr}};
  return e;
}

/// Fails the first `failures` profile creations with a transient error (database unavailable).
class FlakyRepository final : public sg::graph_service::app::GraphRepository {
 public:
  FlakyRepository(sg::graph_service::app::GraphRepository& inner, int failures) : inner_(inner), failures_(failures) {}
  sg::graph_service::domain::Taxonomy load_taxonomy() override { return inner_.load_taxonomy(); }
  std::optional<sg::graph_service::domain::ProfileWithCount> find_profile(std::int64_t id) override {
    return inner_.find_profile(id);
  }
  sg::graph_service::domain::UserPage list_profiles(const sg::graph_service::domain::UserQuery& q) override {
    return inner_.list_profiles(q);
  }
  sg::graph_service::domain::ConnectionList list_connections(std::int64_t id) override {
    return inner_.list_connections(id);
  }
  sg::graph_service::domain::AddedConnection add_connection(std::int64_t a, std::int64_t b, double s) override {
    return inner_.add_connection(a, b, s);
  }
  std::int64_t remove_connection(std::int64_t a, std::int64_t b) override { return inner_.remove_connection(a, b); }
  bool create_member_profile(const sg::graph_service::domain::NewMember& m, const std::string& stream,
                             const std::string& event_id) override {
    if (failures_.fetch_sub(1) > 0) {
      throw sg::common::unavailable("Database unavailable");
    }
    return inner_.create_member_profile(m, stream, event_id);
  }
  void record_dead_letter(const std::string& stream, const std::string& id, const json& fields,
                          const std::string& error) override {
    inner_.record_dead_letter(stream, id, fields, error);
  }
  sg::graph_service::domain::Snapshot snapshot() override { return inner_.snapshot(); }

 private:
  sg::graph_service::app::GraphRepository& inner_;
  std::atomic<int> failures_;
};

class ConsumerTest : public sg::graph_service::testing::GraphDatabaseTest {
 protected:
  void SetUp() override {
    redis_options_ = sg::common::testing::test_redis_options();
    if (!redis_options_) {
      GTEST_SKIP() << "SG_TEST_REDIS_HOST not set";
    }
    GraphDatabaseTest::SetUp();
    if (IsSkipped()) {
      return;
    }
    ASSERT_TRUE(seed_fixtures().loaded);
    redis_ = std::make_unique<sg::common::RedisClient>(*redis_options_);
    options_.stream = sg::common::testing::unique_name("identity.events.test");
    options_.group = "graph-service";
    options_.consumer = "graph-test-1";
    options_.block = 50ms;
  }
  void TearDown() override {
    if (redis_) {
      (void)redis_->del(options_.stream);
    }
  }

  void publish(const sg::common::Event& e) { (void)redis_->xadd(options_.stream, sg::common::to_stream_fields(e)); }

  /// Entries delivered to `consumer` but not acknowledged.
  std::size_t pending_for(const std::string& consumer) {
    return redis_->xreadgroup(options_.stream, options_.group, consumer, 0ms, 100, "0").size();
  }

  std::optional<sg::common::RedisOptions> redis_options_;
  std::unique_ptr<sg::common::RedisClient> redis_;
  IdentityEventsConsumer::Options options_;
};

TEST_F(ConsumerTest, UserRegisteredCreatesTheProfileAndIsAcknowledged) {
  sg::graph_service::app::GraphService graph(*repo_);
  IdentityEventsConsumer consumer(graph, *redis_options_, options_);
  publish(registered("1", 100000001, "ada_l"));
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 1U);
  EXPECT_EQ(consumer.applied_count(), 1U);
  ASSERT_TRUE(repo_->find_profile(100000001));
  EXPECT_EQ(repo_->graph_version(), 2);
  EXPECT_EQ(pending_for(options_.consumer), 0U);
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 0U);
}

TEST_F(ConsumerTest, EventsPublishedBeforeTheGroupExistedAreConsumed) {
  publish(registered("1", 100000001, "early_bird"));  // the stream exists before graph-service's group
  sg::graph_service::app::GraphService graph(*repo_);
  IdentityEventsConsumer consumer(graph, *redis_options_, options_);
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 1U);
  EXPECT_TRUE(repo_->find_profile(100000001));
}

TEST_F(ConsumerTest, DuplicateDeliveriesAreIdempotent) {
  sg::graph_service::app::GraphService graph(*repo_);
  IdentityEventsConsumer consumer(graph, *redis_options_, options_);
  const auto event = registered("7", 100000001, "ada_l");
  publish(event);
  publish(event);  // relay crashed after XADD, before marking the row published
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 2U);
  EXPECT_EQ(consumer.applied_count(), 1U);
  EXPECT_EQ(repo_->graph_version(), 2);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM outbox WHERE type = 'user.created'"), 1);
}

TEST_F(ConsumerTest, PoisonEventsAreDeadLetteredAndAcknowledged) {
  sg::graph_service::app::GraphService graph(*repo_);
  IdentityEventsConsumer consumer(graph, *redis_options_, options_);
  (void)redis_->xadd(options_.stream, {{"event_id", "1"}, {"type", "user.registered"}});  // malformed envelope
  publish(registered("2", 100000002, "cobol_fan", "cobol"));                           // unknown branch
  auto bad_payload = registered("3", 100000003, "bad");
  bad_payload.payload["user_id"] = "not a number";
  publish(bad_payload);
  publish(registered("4", 100000004, "good_one"));  // processing continues after poison
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 4U);
  EXPECT_EQ(consumer.dead_lettered_count(), 3U);
  EXPECT_EQ(scalar<long>("SELECT count(*) FROM dead_letter_events"), 3);
  EXPECT_EQ(scalar<std::string>("SELECT fields->>'type' FROM dead_letter_events ORDER BY id LIMIT 1"),
            "user.registered");
  EXPECT_TRUE(repo_->find_profile(100000004));
  EXPECT_EQ(repo_->graph_version(), 2);
  EXPECT_EQ(pending_for(options_.consumer), 0U);
}

TEST_F(ConsumerTest, TransientFailuresKeepTheEntryPendingUntilItSucceeds) {
  FlakyRepository flaky(*repo_, 2);
  sg::graph_service::app::GraphService graph(flaky);
  IdentityEventsConsumer consumer(graph, *redis_options_, options_);
  publish(registered("1", 100000001, "ada_l"));
  publish(registered("2", 100000002, "bob_b"));
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 0U);  // first attempt fails: nothing acknowledged
  EXPECT_EQ(pending_for(options_.consumer), 2U);     // both delivered, processing stopped at the first
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 0U);  // pending entry retried, fails again
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 2U);  // pending entry, then the new one
  EXPECT_EQ(consumer.dead_lettered_count(), 0U);     // transient failures are never dead-lettered
  EXPECT_TRUE(repo_->find_profile(100000001));
  EXPECT_TRUE(repo_->find_profile(100000002));
  EXPECT_EQ(repo_->graph_version(), 3);
}

TEST_F(ConsumerTest, EntriesLeftPendingByACrashedConsumerAreClaimed) {
  redis_->xgroup_create(options_.stream, options_.group, "0");
  publish(registered("1", 100000001, "ada_l"));
  // Another instance reads the entry and dies before acknowledging it.
  ASSERT_EQ(redis_->xreadgroup(options_.stream, options_.group, "crashed-instance", 0ms, 10, ">").size(), 1U);
  sg::graph_service::app::GraphService graph(*repo_);
  auto options = options_;
  options.claim_min_idle = 0ms;
  IdentityEventsConsumer consumer(graph, *redis_options_, options);
  EXPECT_EQ(consumer.poll_once(*redis_, 50ms), 1U);
  EXPECT_TRUE(repo_->find_profile(100000001));
  EXPECT_EQ(pending_for("crashed-instance"), 0U);
}

TEST_F(ConsumerTest, BackgroundThreadConsumesAndStopsPromptly) {
  sg::graph_service::app::GraphService graph(*repo_);
  IdentityEventsConsumer consumer(graph, *redis_options_, options_);
  consumer.start();
  publish(registered("1", 100000001, "ada_l"));
  publish(registered("2", 100000002, "bob_b"));
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (consumer.applied_count() < 2 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ(consumer.applied_count(), 2U);
  const auto started = std::chrono::steady_clock::now();
  consumer.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - started, 3s);
  EXPECT_EQ(repo_->graph_version(), 3);
}

}  // namespace
