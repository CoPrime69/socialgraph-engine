#pragma once

#include "analytics_engine.hpp"
#include "read_model.hpp"

#include <sg/common/redis.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace sg::analytics {

using SnapshotFetcher = std::function<Snapshot()>;

// Fetches the snapshot from graph-service over HTTP (GET /internal/v1/snapshot).
SnapshotFetcher http_snapshot_fetcher(std::string graph_service_url);

// Keeps the read model current (contracts §7):
//   1. bootstrap from a snapshot (retrying with backoff while graph-service is unavailable);
//   2. XREAD graph.events from the beginning, ignoring events the snapshot already contains;
//   3. on a version gap or an inconsistent event, re-bootstrap from a fresh snapshot.
// Every replica reads every event (plain XREAD, no consumer group), so replicas scale horizontally.
class EventConsumer {
 public:
  struct Options {
    std::string stream = "graph.events";
    bool eager_recompute = true;  // warm the analytics cache after each batch instead of on first read
  };

  EventConsumer(ReadModel& model, AnalyticsEngine& engine, SnapshotFetcher fetch_snapshot,
                sg::common::RedisOptions redis, Options options);
  ~EventConsumer();
  EventConsumer(const EventConsumer&) = delete;
  EventConsumer& operator=(const EventConsumer&) = delete;

  void start();
  void stop();

  [[nodiscard]] std::uint64_t resyncs() const { return resyncs_.load(); }
  [[nodiscard]] std::uint64_t events_applied() const { return applied_.load(); }

 private:
  void run(const std::stop_token& stop);
  bool bootstrap();

  ReadModel& model_;
  AnalyticsEngine& engine_;
  SnapshotFetcher fetch_snapshot_;
  sg::common::RedisOptions redis_options_;
  Options options_;
  std::jthread worker_;
  std::atomic<std::uint64_t> resyncs_{0};
  std::atomic<std::uint64_t> applied_{0};
};

}  // namespace sg::analytics
