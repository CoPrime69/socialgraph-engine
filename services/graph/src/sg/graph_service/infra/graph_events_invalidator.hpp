#pragma once

/// @file graph_events_invalidator.hpp
/// Keeps every replica's ReadCache coherent: tails graph.events (plain XREAD, no consumer group,
/// so every replica sees every event) and invalidates the members each event touches.
///
/// It starts a few seconds before "now" (stream ids are millisecond timestamps), so no event is
/// missed between startup and the first read; re-invalidating is harmless. After a Redis error it
/// clears the whole cache, since events may have been missed while disconnected.

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include <sg/common/redis.hpp>

#include "sg/graph_service/http/read_cache.hpp"

namespace sg::graph_service::infra {

class GraphEventsInvalidator {
 public:
  struct Options {
    std::string stream = "graph.events";
    std::chrono::milliseconds block{1000};
    std::chrono::milliseconds start_lookback{5000};
  };

  GraphEventsInvalidator(http::ReadCache& cache, sg::common::RedisOptions redis, Options options);
  ~GraphEventsInvalidator();
  GraphEventsInvalidator(const GraphEventsInvalidator&) = delete;
  GraphEventsInvalidator& operator=(const GraphEventsInvalidator&) = delete;

  void start();
  void stop();

  /// Invalidates what one stream entry touches; returns the number of members invalidated.
  static int apply(http::ReadCache& cache, const sg::common::StreamEntry& entry);

 private:
  void run(const std::stop_token& stop);

  http::ReadCache& cache_;
  sg::common::RedisOptions redis_options_;
  Options options_;
  std::jthread thread_;
};

}  // namespace sg::graph_service::infra
