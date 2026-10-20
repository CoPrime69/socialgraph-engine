#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "sg/common/redis.hpp"
#include "sg/common/testing/test_env.hpp"

namespace {

using namespace sg::common;
using namespace std::chrono_literals;

class RedisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto options = sg::common::testing::test_redis_options();
    if (!options) {
      GTEST_SKIP() << "SG_TEST_REDIS_HOST not set";
    }
    options_ = *options;
    client_ = std::make_unique<RedisClient>(options_);
    stream_ = sg::common::testing::unique_name("sg.test.stream");
  }
  void TearDown() override {
    if (client_) {
      (void)client_->del(stream_);
    }
  }

  RedisOptions options_;
  std::unique_ptr<RedisClient> client_;
  std::string stream_;
};

TEST_F(RedisTest, PingAndAddRead) {
  EXPECT_TRUE(client_->ping());
  const std::string id1 = client_->xadd(stream_, {{"type", "a"}, {"payload", "{\"x\":1}"}});
  const std::string id2 = client_->xadd(stream_, {{"type", "b"}, {"payload", "with spaces\nand \"quotes\""}});
  EXPECT_NE(id1, id2);
  EXPECT_EQ(client_->xlen(stream_), 2);

  auto entries = client_->xread(stream_, "0", 0ms, 10);
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].id, id1);
  EXPECT_EQ(entries[0].fields.at("type"), "a");
  EXPECT_EQ(entries[1].fields.at("payload"), "with spaces\nand \"quotes\"");

  entries = client_->xread(stream_, id1, 0ms, 10);
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].id, id2);

  const auto range = client_->xrange(stream_);
  ASSERT_EQ(range.size(), 2U);
  EXPECT_EQ(range[1].id, id2);
}

TEST_F(RedisTest, BlockingReadTimesOutEmpty) {
  const auto start = std::chrono::steady_clock::now();
  const auto entries = client_->xread(stream_, "$", 200ms, 10);
  EXPECT_TRUE(entries.empty());
  EXPECT_GE(std::chrono::steady_clock::now() - start, 150ms);
}

TEST_F(RedisTest, ConsumerGroupDeliversAcksAndRedeliversPending) {
  client_->xgroup_create(stream_, "workers", "0");
  client_->xgroup_create(stream_, "workers", "0");  // BUSYGROUP ignored
  const std::string id = client_->xadd(stream_, {{"k", "v"}});

  auto delivered = client_->xreadgroup(stream_, "workers", "c1", 100ms, 10);
  ASSERT_EQ(delivered.size(), 1U);
  EXPECT_EQ(delivered[0].id, id);
  EXPECT_TRUE(client_->xreadgroup(stream_, "workers", "c1", 0ms, 10).empty());  // nothing new

  // Not acknowledged yet: still pending for c1.
  auto pending = client_->xreadgroup(stream_, "workers", "c1", 0ms, 10, "0");
  ASSERT_EQ(pending.size(), 1U);
  EXPECT_EQ(pending[0].fields.at("k"), "v");

  client_->xack(stream_, "workers", id);
  EXPECT_TRUE(client_->xreadgroup(stream_, "workers", "c1", 0ms, 10, "0").empty());
}

TEST_F(RedisTest, AutoClaimTakesOverEntriesOfDeadConsumers) {
  client_->xgroup_create(stream_, "workers", "0");
  const std::string id = client_->xadd(stream_, {{"k", "v"}});
  ASSERT_EQ(client_->xreadgroup(stream_, "workers", "dead", 0ms, 10).size(), 1U);
  std::this_thread::sleep_for(50ms);
  const auto claimed = client_->xautoclaim(stream_, "workers", "alive", 20ms, "0-0", 10);
  ASSERT_EQ(claimed.entries.size(), 1U);
  EXPECT_EQ(claimed.entries[0].id, id);
  EXPECT_EQ(client_->xreadgroup(stream_, "workers", "alive", 0ms, 10, "0").size(), 1U);
}

TEST_F(RedisTest, ErrorsAndReconnect) {
  EXPECT_THROW((void)client_->xreadgroup(stream_, "no-such-group", "c", 0ms, 1), RedisError);
  // The client stays usable after an error reply.
  EXPECT_TRUE(client_->ping());

  RedisOptions closed = options_;
  closed.port = 1;
  closed.timeout = 200ms;
  RedisClient unreachable(closed);
  EXPECT_FALSE(unreachable.ping());
  EXPECT_THROW((void)unreachable.xadd(stream_, {{"k", "v"}}), RedisError);
}

}  // namespace
