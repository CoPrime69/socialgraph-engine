#pragma once

/// @file rate_limit.hpp
/// In-process fixed-window rate limiter keyed by an arbitrary string (client IP, handle, ...).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace sg::common {

/// Allows at most `max_events` events per key in each window of `window` seconds. Thread-safe.
/// Memory is bounded: expired keys are evicted periodically and, if more than `max_keys` keys are
/// still active, the oldest windows are evicted first. `max_events == 0` disables limiting.
class RateLimiter {
 public:
  using Clock = std::chrono::steady_clock;

  RateLimiter(std::size_t max_events, std::chrono::seconds window, std::size_t max_keys = 100'000);

  /// Records one event for `key`. std::nullopt when allowed, otherwise the time until the key's
  /// window resets (>= 1 s); a rejected event is not counted.
  [[nodiscard]] std::optional<std::chrono::seconds> check(const std::string& key);
  /// Same with an explicit clock reading (tests).
  [[nodiscard]] std::optional<std::chrono::seconds> check_at(const std::string& key, Clock::time_point now);

  /// Time until `key`'s window resets (>= 1 s) when it is already full, else std::nullopt. Records
  /// nothing: lets callers limit only some outcomes, e.g. check before a login and count failures
  /// with check() afterwards.
  [[nodiscard]] std::optional<std::chrono::seconds> retry_after(const std::string& key) const;
  /// Same with an explicit clock reading (tests).
  [[nodiscard]] std::optional<std::chrono::seconds> retry_after_at(const std::string& key, Clock::time_point now) const;

  /// Number of keys currently tracked.
  [[nodiscard]] std::size_t tracked_keys() const;

 private:
  struct Window {
    Clock::time_point start;
    std::size_t count;
  };
  void evict(Clock::time_point now);

  std::size_t max_events_;
  std::chrono::seconds window_;
  std::size_t max_keys_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Window> windows_;
  std::uint64_t calls_since_sweep_ = 0;
};

}  // namespace sg::common
