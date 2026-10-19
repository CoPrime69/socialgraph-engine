#include "sg/graph_service/http/read_cache.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <sg/common/errors.hpp>

namespace {

using sg::graph_service::http::ReadCache;
using Kind = ReadCache::Kind;
using sg::graph_service::http::ReadCacheOptions;

TEST(ReadCache, MemberEntriesAreCachedUntilInvalidated) {
  ReadCache cache;
  int renders = 0;
  auto render = [&] { return "v" + std::to_string(++renders); };
  EXPECT_EQ(*cache.member(Kind::profile, 7, render), "v1");
  EXPECT_EQ(*cache.member(Kind::profile, 7, render), "v1");
  EXPECT_EQ(*cache.member(Kind::connections, 7, render), "v2") << "kinds are separate entries";
  cache.invalidate(7);
  EXPECT_EQ(*cache.member(Kind::profile, 7, render), "v3");
  EXPECT_EQ(*cache.member(Kind::connections, 7, render), "v4");
  EXPECT_EQ(*cache.member(Kind::profile, 8, render), "v5");
}

TEST(ReadCache, FailedRendersAreNotCached) {
  ReadCache cache;
  int renders = 0;
  EXPECT_THROW((void)cache.member(Kind::profile, 1,
                                  [&]() -> std::string {
                                    ++renders;
                                    throw sg::common::not_found("no such user");
                                  }),
               sg::common::ApiError);
  EXPECT_EQ(*cache.member(Kind::profile, 1, [&] { return std::to_string(++renders); }), "2");
}

TEST(ReadCache, LoadThatRacesWithAnInvalidationIsNotStored) {
  ReadCache cache;
  // The render observes the old state, then the write commits and invalidates before it returns.
  const auto stale = cache.member(Kind::profile, 5, [&] {
    cache.invalidate(5);
    return std::string("old");
  });
  EXPECT_EQ(*stale, "old");  // the caller still gets what it read
  EXPECT_EQ(*cache.member(Kind::profile, 5, [] { return std::string("new"); }), "new");

  const auto during_clear = cache.member(Kind::connections, 6, [&] {
    cache.clear();
    return std::string("old");
  });
  EXPECT_EQ(*cache.member(Kind::connections, 6, [] { return std::string("new"); }), "new");
}

TEST(ReadCache, SearchPagesExpire) {
  ReadCacheOptions options;
  options.search_ttl = std::chrono::milliseconds(50);
  ReadCache cache(options);
  int renders = 0;
  auto render = [&] { return std::to_string(++renders); };
  EXPECT_EQ(*cache.search("q", render), "1");
  EXPECT_EQ(*cache.search("q", render), "1");
  cache.invalidate(1);  // per-member invalidation does not touch searches
  EXPECT_EQ(*cache.search("q", render), "1");
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  EXPECT_EQ(*cache.search("q", render), "2");
}

TEST(ReadCache, DisabledCacheAlwaysRenders) {
  ReadCacheOptions options;
  options.member_entries = 0;
  options.search_entries = 0;
  ReadCache cache(options);
  int renders = 0;
  auto render = [&] { return std::to_string(++renders); };
  (void)cache.member(Kind::profile, 1, render);
  (void)cache.member(Kind::profile, 1, render);
  (void)cache.search("q", render);
  (void)cache.search("q", render);
  EXPECT_EQ(renders, 4);
}

TEST(ReadCache, ConcurrentReadersAndInvalidationsNeverServeAnOldVersionAfterTheWrite) {
  ReadCache cache;
  std::atomic<int> version{0};
  std::atomic<bool> stop{false};
  std::vector<std::jthread> readers;
  for (int t = 0; t < 4; ++t) {
    readers.emplace_back([&] {
      while (!stop) {
        (void)cache.member(Kind::profile, 42, [&] { return std::to_string(version.load()); });
      }
    });
  }
  int stale = 0;
  for (int v = 1; v <= 2000; ++v) {
    version = v;           // "commit"
    cache.invalidate(42);  // then invalidate, like the write path
    const auto seen = std::stoi(*cache.member(Kind::profile, 42, [&] { return std::to_string(version.load()); }));
    stale += seen < v ? 1 : 0;
  }
  stop = true;
  EXPECT_EQ(stale, 0) << "a value loaded before the write was served after it";
}

}  // namespace
