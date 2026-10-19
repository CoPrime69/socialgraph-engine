#pragma once

/// @file identity_events_consumer.hpp
/// Consumes `identity.events` with the consumer group `graph-service` (contracts §7).
///
/// Delivery is at-least-once: an entry is acknowledged (XACK) only after its effects are committed
/// (or after it was moved to dead_letter_events when it can never be applied). On start the
/// consumer first re-reads its own pending entries (a crash between commit and XACK), and
/// periodically claims entries that other, dead consumers left pending (XAUTOCLAIM). Transient
/// failures (database or Redis down) are retried with exponential backoff and never dead-lettered.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <sg/common/redis.hpp>

#include "sg/graph_service/app/graph_service.hpp"

namespace sg::graph_service::infra {

class IdentityEventsConsumer {
 public:
  struct Options {
    std::string stream = "identity.events";
    std::string group = "graph-service";
    std::string consumer;  ///< unique per instance; default: the host name
    std::chrono::milliseconds block{2000};
    std::size_t batch_size = 100;
    std::chrono::milliseconds claim_min_idle{std::chrono::seconds(60)};
    std::chrono::milliseconds claim_interval{std::chrono::seconds(30)};
    int max_attempts = 5;  ///< for unexpected (non-transient) errors before dead-lettering
  };

  IdentityEventsConsumer(app::GraphService& service, sg::common::RedisOptions redis, Options options);
  ~IdentityEventsConsumer();
  IdentityEventsConsumer(const IdentityEventsConsumer&) = delete;
  IdentityEventsConsumer& operator=(const IdentityEventsConsumer&) = delete;

  void start();
  void stop();

  /// One synchronous pass (tests, tools): creates the group if needed, handles this consumer's
  /// pending entries, claims stale entries of other consumers, then reads new entries (waiting up to
  /// `block`). Returns the number of entries acknowledged.
  std::size_t poll_once(sg::common::RedisClient& redis, std::chrono::milliseconds block);

  [[nodiscard]] std::uint64_t applied_count() const noexcept { return applied_.load(); }
  [[nodiscard]] std::uint64_t dead_lettered_count() const noexcept { return dead_lettered_.load(); }
  [[nodiscard]] const Options& options() const noexcept { return options_; }

 private:
  struct BatchResult {
    std::size_t acknowledged = 0;
    bool retry_later = false;  ///< an entry failed transiently and stays pending
  };

  void run();
  BatchResult handle_batch(sg::common::RedisClient& redis, const std::vector<sg::common::StreamEntry>& entries);
  /// True when the entry was acknowledged.
  bool handle(sg::common::RedisClient& redis, const sg::common::StreamEntry& entry);
  bool dead_letter(sg::common::RedisClient& redis, const sg::common::StreamEntry& entry, const std::string& error);
  BatchResult claim_stale(sg::common::RedisClient& redis);
  bool sleep_for(std::chrono::milliseconds duration);  // false when stopping

  app::GraphService& service_;
  sg::common::RedisOptions redis_options_;
  Options options_;

  std::unordered_map<std::string, int> attempts_;  // entry id -> failed attempts (consumer thread only)
  std::atomic<std::uint64_t> applied_{0};
  std::atomic<std::uint64_t> dead_lettered_{0};

  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::thread thread_;
};

/// The host name, or "graph-service" when unavailable (consumer name default).
[[nodiscard]] std::string default_consumer_name();

}  // namespace sg::graph_service::infra
