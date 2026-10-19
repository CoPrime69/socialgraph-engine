#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "sg/common/rate_limit.hpp"

namespace {

using namespace sg::common;
using namespace std::chrono_literals;

TEST(RateLimiter, AllowsUpToTheLimitPerWindow) {
  RateLimiter limiter(3, 60s);
  const auto t0 = RateLimiter::Clock::now();
  EXPECT_FALSE(limiter.check_at("ip", t0));
  EXPECT_FALSE(limiter.check_at("ip", t0 + 1s));
  EXPECT_FALSE(limiter.check_at("ip", t0 + 2s));
  const auto retry = limiter.check_at("ip", t0 + 10s);
  ASSERT_TRUE(retry);
  EXPECT_EQ(*retry, 50s);
  EXPECT_FALSE(limiter.check_at("other", t0 + 10s));  // keys are independent
  EXPECT_FALSE(limiter.check_at("ip", t0 + 60s));     // new window
}

TEST(RateLimiter, RetryAfterIsAtLeastOneSecond) {
  RateLimiter limiter(1, 5s);
  const auto t0 = RateLimiter::Clock::now();
  EXPECT_FALSE(limiter.check_at("k", t0));
  const auto retry = limiter.check_at("k", t0 + 4999ms);
  ASSERT_TRUE(retry);
  EXPECT_GE(*retry, 1s);
}

TEST(RateLimiter, RetryAfterQueriesWithoutRecording) {
  RateLimiter limiter(2, 60s);
  const auto t0 = RateLimiter::Clock::now();
  EXPECT_FALSE(limiter.retry_after_at("k", t0));  // unknown key
  EXPECT_FALSE(limiter.check_at("k", t0));
  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(limiter.retry_after_at("k", t0 + 1s));  // queries are not counted
  }
  EXPECT_FALSE(limiter.check_at("k", t0 + 2s));
  const auto retry = limiter.retry_after_at("k", t0 + 20s);
  ASSERT_TRUE(retry);
  EXPECT_EQ(*retry, 40s);
  EXPECT_FALSE(limiter.retry_after_at("k", t0 + 60s));  // window over
  EXPECT_FALSE(RateLimiter(0, 60s).retry_after("k"));   // disabled
}

TEST(RateLimiter, ZeroDisablesLimiting) {
  RateLimiter limiter(0, 60s);
  for (int i = 0; i < 1000; ++i) {
    EXPECT_FALSE(limiter.check("k"));
  }
}

TEST(RateLimiter, MemoryIsBounded) {
  RateLimiter limiter(5, 60s, 100);
  const auto t0 = RateLimiter::Clock::now();
  for (int i = 0; i < 10'000; ++i) {
    (void)limiter.check_at("key" + std::to_string(i), t0 + std::chrono::milliseconds(i));
  }
  EXPECT_LE(limiter.tracked_keys(), 100U);
  // Expired windows are swept.
  (void)limiter.check_at("late", t0 + 3600s);
  EXPECT_LE(limiter.tracked_keys(), 2U);
}

TEST(RateLimiter, IsThreadSafe) {
  RateLimiter limiter(1000, 60s);
  std::atomic<int> allowed{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 500; ++i) {
        if (!limiter.check("shared")) {
          allowed.fetch_add(1);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(allowed.load(), 1000);
}

}  // namespace
