#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "sg/common/events.hpp"
#include "sg/common/outbox.hpp"
#include "sg/common/testing/test_env.hpp"

namespace {

using namespace sg::common;
using namespace std::chrono_literals;
using sg::common::testing::TempDatabase;

const std::vector<Migration> kOutboxSchema{
    {1, "outbox",
     "CREATE TABLE outbox (id bigserial PRIMARY KEY, type text NOT NULL, payload jsonb NOT NULL,"
     " occurred_at timestamptz NOT NULL DEFAULT now(), published_at timestamptz);"
     "CREATE INDEX outbox_unpublished_idx ON outbox (id) WHERE published_at IS NULL;"
     "CREATE TABLE items (id integer PRIMARY KEY);"},
};

class OutboxTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto url = sg::common::testing::test_database_url();
    const auto redis = sg::common::testing::test_redis_options();
    if (!url || !redis) {
      GTEST_SKIP() << "SG_TEST_DATABASE_URL and SG_TEST_REDIS_HOST are required";
    }
    redis_options_ = *redis;
    db_ = std::make_unique<TempDatabase>(*url, "sg_common_outbox");
    PgPoolOptions options;
    options.conninfo = db_->url();
    options.size = 4;
    pool_ = std::make_unique<PgPool>(options);
    run_migrations(*pool_, kOutboxSchema, 1);
    stream_ = sg::common::testing::unique_name("sg.test.outbox");
    redis_ = std::make_unique<RedisClient>(redis_options_);
  }
  void TearDown() override {
    if (redis_) {
      (void)redis_->del(stream_);
    }
    pool_.reset();
  }

  void insert_item_with_event(int id) {
    pool_->with_transaction([id](pqxx::work& tx) {
      tx.exec_params("INSERT INTO items (id) VALUES ($1)", id);
      enqueue_event(tx, "item.created", {{"item_id", id}});
    });
  }

  long unpublished() {
    return pool_->with_transaction(
        [](pqxx::work& tx) { return tx.query_value<long>("SELECT count(*) FROM outbox WHERE published_at IS NULL"); });
  }

  /// Waits until the stream has `n` entries (or 5 s pass).
  std::vector<StreamEntry> wait_for_entries(std::size_t n) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    std::vector<StreamEntry> entries;
    while (std::chrono::steady_clock::now() < deadline) {
      entries = redis_->xrange(stream_);
      if (entries.size() >= n) {
        break;
      }
      std::this_thread::sleep_for(20ms);
    }
    return entries;
  }

  RedisOptions redis_options_;
  std::unique_ptr<TempDatabase> db_;
  std::unique_ptr<PgPool> pool_;
  std::unique_ptr<RedisClient> redis_;
  std::string stream_;
};

TEST_F(OutboxTest, RolledBackTransactionLeavesNoEvent) {
  EXPECT_THROW(pool_->with_transaction([](pqxx::work& tx) {
    tx.exec("INSERT INTO items (id) VALUES (1)");
    enqueue_event(tx, "item.created", {{"item_id", 1}});
    throw std::runtime_error("business rule failed");
  }),
               std::runtime_error);
  EXPECT_EQ(pool_->with_transaction([](pqxx::work& tx) { return tx.query_value<long>("SELECT count(*) FROM outbox"); }), 0);
}

TEST_F(OutboxTest, RelayPublishesInOrderAndMarksRows) {
  for (int i = 1; i <= 5; ++i) {
    insert_item_with_event(i);
  }
  OutboxRelay relay(*pool_, redis_options_, stream_, 20ms);
  relay.start();
  const auto entries = wait_for_entries(5);
  relay.stop();
  ASSERT_EQ(entries.size(), 5U);
  std::int64_t previous_id = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const Event event = from_stream_entry(entries[i]);
    EXPECT_EQ(event.type, "item.created");
    EXPECT_EQ(event.schema, 1);
    EXPECT_EQ(event.payload["item_id"], static_cast<int>(i) + 1);
    const std::int64_t event_id = std::stoll(event.event_id);
    EXPECT_GT(event_id, previous_id);  // outbox id order
    previous_id = event_id;
    EXPECT_EQ(event.occurred_at.size(), 24U) << event.occurred_at;
  }
  EXPECT_EQ(unpublished(), 0);
  EXPECT_EQ(relay.published_count(), 5U);
}

TEST_F(OutboxTest, PublishBatchOnceRespectsBatchSize) {
  for (int i = 1; i <= 7; ++i) {
    insert_item_with_event(i);
  }
  OutboxRelay relay(*pool_, redis_options_, stream_);
  relay.set_batch_size(3);
  EXPECT_EQ(relay.publish_batch_once(*redis_), 3U);
  EXPECT_EQ(unpublished(), 4);
  EXPECT_EQ(relay.publish_batch_once(*redis_), 3U);
  EXPECT_EQ(relay.publish_batch_once(*redis_), 1U);
  EXPECT_EQ(relay.publish_batch_once(*redis_), 0U);
  EXPECT_EQ(redis_->xlen(stream_), 7);
}

TEST_F(OutboxTest, OnlyOneLeaderPublishesAndFollowerTakesOver) {
  OutboxRelay first(*pool_, redis_options_, stream_, 20ms);
  OutboxRelay second(*pool_, redis_options_, stream_, 20ms);
  first.start();
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!first.is_leader() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  ASSERT_TRUE(first.is_leader());
  second.start();
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(second.is_leader());

  insert_item_with_event(1);
  EXPECT_EQ(wait_for_entries(1).size(), 1U);

  first.stop();  // releases the advisory lock
  const auto takeover_deadline = std::chrono::steady_clock::now() + 5s;
  while (!second.is_leader() && std::chrono::steady_clock::now() < takeover_deadline) {
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_TRUE(second.is_leader());
  insert_item_with_event(2);
  const auto entries = wait_for_entries(2);
  second.stop();
  ASSERT_EQ(entries.size(), 2U);  // each event exactly once
  EXPECT_EQ(from_stream_entry(entries[1]).payload["item_id"], 2);
}

TEST_F(OutboxTest, StopIsPrompt) {
  OutboxRelay relay(*pool_, redis_options_, stream_, 10s);
  relay.start();
  std::this_thread::sleep_for(100ms);
  const auto start = std::chrono::steady_clock::now();
  relay.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
}

TEST_F(OutboxTest, RedisOutageIsRetried) {
  insert_item_with_event(1);
  RedisOptions broken = redis_options_;
  broken.port = 1;
  broken.timeout = 100ms;
  OutboxRelay relay(*pool_, broken, stream_, 20ms);
  relay.start();
  std::this_thread::sleep_for(300ms);
  relay.stop();
  EXPECT_EQ(unpublished(), 1);  // nothing lost while Redis was unreachable
  OutboxRelay healthy(*pool_, redis_options_, stream_, 20ms);
  healthy.start();
  EXPECT_EQ(wait_for_entries(1).size(), 1U);
  healthy.stop();
  EXPECT_EQ(unpublished(), 0);
}

}  // namespace
