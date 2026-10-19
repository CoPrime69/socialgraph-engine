#pragma once

/// @file outbox.hpp
/// Transactional outbox (contracts.md §7). Each service's own migration creates
///
///   CREATE TABLE outbox (id bigserial PRIMARY KEY, type text NOT NULL, payload jsonb NOT NULL,
///                        occurred_at timestamptz NOT NULL DEFAULT now(), published_at timestamptz);
///   CREATE INDEX outbox_unpublished_idx ON outbox (id) WHERE published_at IS NULL;
///
/// enqueue_event() writes the event in the caller's transaction, so it is committed (or rolled
/// back) together with the state change. OutboxRelay publishes committed rows to a Redis Stream.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "sg/common/pqxx.hpp"

#include "sg/common/pg.hpp"
#include "sg/common/redis.hpp"

namespace sg::common {

/// Inserts an unpublished event into `outbox` within `tx`; returns its id (the future event_id).
std::int64_t enqueue_event(pqxx::work& tx, const std::string& type, const nlohmann::json& payload);

/// Inserts several events with one statement, in the given order (ids increase along the vector).
void enqueue_events(pqxx::work& tx, const std::vector<std::pair<std::string, nlohmann::json>>& events);

/// Background thread that publishes unpublished outbox rows, in id order, to `stream` with XADD
/// (event_id = outbox id) and then marks them published. Delivery is at-least-once: a crash between
/// XADD and the UPDATE re-publishes those rows, so consumers must be idempotent.
///
/// Only one relay per database publishes at a time: the leader holds a session-level
/// pg_try_advisory_lock on a dedicated connection; followers retry periodically and take over when
/// the leader's connection goes away. Redis and PostgreSQL errors are logged and retried with
/// exponential backoff (up to 5 s). stop() wakes the thread immediately.
class OutboxRelay {
 public:
  OutboxRelay(PgPool& pool, RedisOptions redis, std::string stream,
              std::chrono::milliseconds poll_interval = std::chrono::milliseconds(100));
  ~OutboxRelay();
  OutboxRelay(const OutboxRelay&) = delete;
  OutboxRelay& operator=(const OutboxRelay&) = delete;

  /// Approximate MAXLEN for XADD (0 = never trim, the default). Call before start().
  void set_stream_max_length(std::size_t approx_max_len) noexcept { max_len_ = approx_max_len; }
  /// Published rows older than this are deleted by the leader (0 = keep forever; default 7 days).
  /// Call before start().
  void set_retention(std::chrono::seconds retention) noexcept { retention_ = retention; }
  /// Rows per batch (default 100). Call before start().
  void set_batch_size(std::size_t batch_size) noexcept { batch_size_ = batch_size == 0 ? 1 : batch_size; }

  void start();
  void stop();

  [[nodiscard]] bool is_leader() const noexcept { return leader_.load(); }
  [[nodiscard]] std::uint64_t published_count() const noexcept { return published_.load(); }
  /// Advisory lock key used for leader election (derived from the stream name).
  [[nodiscard]] std::int64_t lock_key() const noexcept { return lock_key_; }

  /// Publishes one batch synchronously from the calling thread, without leader election.
  /// Returns the number of rows published. Intended for tests and tools.
  std::size_t publish_batch_once(RedisClient& redis);

 private:
  void run();
  bool sleep_for(std::chrono::milliseconds duration);  // false when stopping

  PgPool& pool_;
  RedisOptions redis_options_;
  std::string stream_;
  std::chrono::milliseconds poll_interval_;
  std::size_t max_len_ = 0;
  std::chrono::seconds retention_{std::chrono::hours(24 * 7)};
  std::size_t batch_size_ = 100;
  std::int64_t lock_key_;

  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::thread thread_;
  std::atomic<bool> leader_{false};
  std::atomic<std::uint64_t> published_{0};
};

}  // namespace sg::common
