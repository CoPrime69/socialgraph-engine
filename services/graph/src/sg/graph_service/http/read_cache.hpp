#pragma once

/// @file read_cache.hpp
/// In-process cache of rendered graph-service responses (cache-aside), so hot reads skip
/// PostgreSQL and JSON rendering.
///
/// Per-member entries (a profile, a member's connection list) are invalidated by member id:
/// immediately by this replica after its own writes commit, and by every replica when the
/// matching `connection.*` / `user.created` event arrives on graph.events (GraphEventsInvalidator).
/// A striped generation counter closes the race between a slow load and an invalidation: a value
/// loaded before an invalidation of its member is never stored after it.
///
/// Search pages list many members, so they are not invalidated per member; they live for
/// `search_ttl` instead (bounded staleness, like the refresh interval of a search index).

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <sg/common/lru_cache.hpp>

namespace sg::graph_service::http {

struct ReadCacheOptions {
  std::size_t member_entries = 100'000;  ///< profiles + connection lists; 0 disables the cache
  std::size_t search_entries = 20'000;   ///< search pages; 0 disables search caching
  std::chrono::milliseconds search_ttl{2000};
};

class ReadCache {
 public:
  enum class Kind : std::uint8_t { profile, connections };

  explicit ReadCache(ReadCacheOptions options = {});

  /// Cached body for (kind, member), or `render()` stored and returned. `render` may throw (e.g.
  /// 404); nothing is cached then.
  std::shared_ptr<const std::string> member(Kind kind, std::int64_t member_id,
                                            const std::function<std::string()>& render);

  /// Cached search page for `key`, or `render()` stored for search_ttl and returned.
  std::shared_ptr<const std::string> search(const std::string& key, const std::function<std::string()>& render);

  /// Drops every entry of `member_id`; loads that started earlier will not be stored.
  void invalidate(std::int64_t member_id);

  /// Drops everything (e.g. after missing events).
  void clear();

  struct Stats {
    std::size_t member_hits = 0;
    std::size_t member_misses = 0;
    std::size_t search_hits = 0;
    std::size_t search_misses = 0;
  };
  [[nodiscard]] Stats stats() const;

 private:
  static constexpr std::size_t kStripes = 4096;
  using Clock = std::chrono::steady_clock;

  struct SearchEntry {
    std::shared_ptr<const std::string> body;
    Clock::time_point expires_at;
  };

  [[nodiscard]] static std::int64_t key(Kind kind, std::int64_t member_id) noexcept {
    return member_id * 2 + static_cast<std::int64_t>(kind);
  }
  [[nodiscard]] std::atomic<std::uint64_t>& generation(std::int64_t member_id) noexcept {
    return generations_[static_cast<std::uint64_t>(member_id) % kStripes];
  }

  ReadCacheOptions options_;
  std::unique_ptr<sg::common::LruCache<std::int64_t, std::shared_ptr<const std::string>>> members_;
  std::unique_ptr<sg::common::LruCache<std::string, SearchEntry>> searches_;
  std::array<std::atomic<std::uint64_t>, kStripes> generations_{};
  std::atomic<std::uint64_t> clears_{0};
};

}  // namespace sg::graph_service::http
