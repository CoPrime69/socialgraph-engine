#include "sg/common/lru_cache.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace sg::common {
namespace {

TEST(LruCache, GetReturnsWhatWasPut) {
  LruCache<std::string, int, 1> cache(4);
  EXPECT_FALSE(cache.get("a").has_value());
  cache.put("a", 1);
  EXPECT_EQ(cache.get("a"), 1);
  cache.put("a", 2);  // replace
  EXPECT_EQ(cache.get("a"), 2);
  EXPECT_EQ(cache.stats().size, 1U);
}

TEST(LruCache, EvictsLeastRecentlyUsed) {
  LruCache<int, int, 1> cache(3);
  cache.put(1, 1);
  cache.put(2, 2);
  cache.put(3, 3);
  ASSERT_TRUE(cache.get(1).has_value());  // 1 is now most recent; 2 is the oldest
  cache.put(4, 4);
  EXPECT_FALSE(cache.get(2).has_value());
  EXPECT_TRUE(cache.get(1).has_value());
  EXPECT_TRUE(cache.get(3).has_value());
  EXPECT_TRUE(cache.get(4).has_value());
  EXPECT_EQ(cache.stats().size, 3U);
}

TEST(LruCache, CapacityIsSpreadOverShards) {
  LruCache<int, int, 4> cache(100);
  EXPECT_EQ(cache.capacity(), 100U);
  for (int i = 0; i < 1000; ++i) cache.put(i, i);
  EXPECT_LE(cache.stats().size, 100U);
  LruCache<int, int, 16> tiny(1);  // never zero entries per shard
  tiny.put(1, 1);
  EXPECT_EQ(tiny.get(1), 1);
}

TEST(LruCache, GetOrLoadLoadsOnlyOnMiss) {
  LruCache<std::string, std::string> cache(10);
  int loads = 0;
  auto load = [&] {
    ++loads;
    return std::string("value");
  };
  EXPECT_EQ(cache.get_or_load("k", load), "value");
  EXPECT_EQ(cache.get_or_load("k", load), "value");
  EXPECT_EQ(loads, 1);
  const auto stats = cache.stats();
  EXPECT_EQ(stats.hits, 1U);
  EXPECT_EQ(stats.misses, 1U);
}

TEST(LruCache, LoadFailuresAreNotCached) {
  LruCache<int, int> cache(10);
  EXPECT_THROW(cache.get_or_load(1, []() -> int { throw std::runtime_error("db down"); }), std::runtime_error);
  EXPECT_FALSE(cache.get(1).has_value());
}

TEST(LruCache, ConcurrentAccessIsSafe) {
  LruCache<int, int> cache(256);
  std::atomic<int> wrong{0};
  std::vector<std::jthread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 20000; ++i) {
        const int key = (i * 7 + t) % 1024;
        const int value = cache.get_or_load(key, [key] { return key * 2; });
        if (value != key * 2) ++wrong;
      }
    });
  }
  threads.clear();
  EXPECT_EQ(wrong.load(), 0);
  EXPECT_LE(cache.stats().size, 256U);
}

TEST(LruCache, ClearEmptiesEveryShard) {
  LruCache<int, int> cache(64);
  for (int i = 0; i < 64; ++i) cache.put(i, i);
  cache.clear();
  EXPECT_EQ(cache.stats().size, 0U);
  EXPECT_FALSE(cache.get(5).has_value());
}

}  // namespace
}  // namespace sg::common
