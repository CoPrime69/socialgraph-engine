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

/// A search load listing `ids`, each rendered as {"id":<id>,"v":<loads>}.
struct FakeSearch {
  std::vector<std::int64_t> ids;
  int loads = 0;
  int profile_renders = 0;

  ReadCache::LoadedSearch load() {
    ++loads;
    ReadCache::LoadedSearch page{{}, 42, 20, 0};
    for (const auto id : ids) {
      page.items.emplace_back(id, profile(id, "load" + std::to_string(loads)));
    }
    return page;
  }
  std::string render(std::int64_t id) { return profile(id, "render" + std::to_string(++profile_renders)); }
  static std::string profile(std::int64_t id, const std::string& v) {
    return "{\"id\":" + std::to_string(id) + ",\"v\":\"" + v + "\"}";
  }
  std::string get(ReadCache& cache, const std::string& key) {
    return *cache.search(key, [&] { return load(); }, [&](std::int64_t id) { return render(id); });
  }
};

TEST(ReadCache, SearchPagesKeepIdsAndRenderFreshProfiles) {
  ReadCache cache;
  FakeSearch search{{3, 1}};
  // The miss renders what it loaded, in order, with the page fields in nlohmann's key order.
  EXPECT_EQ(search.get(cache, "q"),
            R"({"items":[{"id":3,"v":"load1"},{"id":1,"v":"load1"}],"limit":20,"offset":0,"total":42})");
  // A hit reuses the ids and takes each profile from the member cache (rendered once here).
  EXPECT_EQ(search.get(cache, "q"),
            R"({"items":[{"id":3,"v":"render1"},{"id":1,"v":"render2"}],"limit":20,"offset":0,"total":42})");
  EXPECT_EQ(search.get(cache, "q"),
            R"({"items":[{"id":3,"v":"render1"},{"id":1,"v":"render2"}],"limit":20,"offset":0,"total":42})");
  // A write to member 1 invalidates its profile; the next search shows the new one.
  cache.invalidate(1);
  EXPECT_EQ(search.get(cache, "q"),
            R"({"items":[{"id":3,"v":"render1"},{"id":1,"v":"render3"}],"limit":20,"offset":0,"total":42})");
  EXPECT_EQ(search.loads, 1);
  // The member cache is shared with the profile route.
  EXPECT_EQ(*cache.member(Kind::profile, 3, [] { return std::string("unused"); }), R"({"id":3,"v":"render1"})");

  FakeSearch empty{{}};
  EXPECT_EQ(empty.get(cache, "none"), R"({"items":[],"limit":20,"offset":0,"total":42})");
}

TEST(ReadCache, SearchIdListsExpire) {
  ReadCacheOptions options;
  options.search_ttl = std::chrono::milliseconds(50);
  ReadCache cache(options);
  FakeSearch search{{1}};
  (void)search.get(cache, "q");
  (void)search.get(cache, "q");
  EXPECT_EQ(search.loads, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  (void)search.get(cache, "q");
  EXPECT_EQ(search.loads, 2);
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
  EXPECT_EQ(renders, 2);
  FakeSearch search{{1}};
  (void)search.get(cache, "q");
  (void)search.get(cache, "q");
  EXPECT_EQ(search.loads, 2);
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
