#include "event_consumer.hpp"

#include <sg/common/log.hpp>

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace sg::analytics {

namespace {

using namespace std::chrono_literals;
using sg::common::log_info;
using sg::common::log_warn;

// Sleeps in short slices so stop() is honoured promptly.
void interruptible_sleep(const std::stop_token& stop, std::chrono::milliseconds total) {
  const auto deadline = std::chrono::steady_clock::now() + total;
  while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::min<std::chrono::milliseconds>(50ms, total));
  }
}

}  // namespace

SnapshotFetcher http_snapshot_fetcher(std::string graph_service_url) {
  return [url = std::move(graph_service_url)]() {
    httplib::Client client(url);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(30, 0);
    auto response = client.Get("/internal/v1/snapshot");
    if (!response) {
      throw std::runtime_error("snapshot request failed: " + httplib::to_string(response.error()));
    }
    if (response->status != 200) {
      throw std::runtime_error("snapshot request returned HTTP " + std::to_string(response->status));
    }
    return parse_snapshot(nlohmann::json::parse(response->body));
  };
}

EventConsumer::EventConsumer(ReadModel& model, AnalyticsEngine& engine, SnapshotFetcher fetch_snapshot,
                             sg::common::RedisOptions redis, Options options)
    : model_(model),
      engine_(engine),
      fetch_snapshot_(std::move(fetch_snapshot)),
      redis_options_(std::move(redis)),
      options_(std::move(options)) {}

EventConsumer::~EventConsumer() { stop(); }

void EventConsumer::start() {
  worker_ = std::jthread([this](const std::stop_token& stop) { run(stop); });
}

void EventConsumer::stop() {
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
}

bool EventConsumer::bootstrap() {
  try {
    auto snapshot = fetch_snapshot_();
    const auto users = snapshot.users.size();
    const auto edges = snapshot.edges.size();
    model_.reset(snapshot.version, std::move(snapshot.users), std::move(snapshot.edges));
    log_info("read model bootstrapped", {{"graph_version", model_.version()}, {"users", users}, {"edges", edges}});
    return true;
  } catch (const std::exception& e) {
    log_warn("snapshot bootstrap failed", {{"error", e.what()}});
    return false;
  }
}

void EventConsumer::run(const std::stop_token& stop) {
  std::optional<sg::common::RedisClient> redis;
  std::string last_id = "0";  // replay from the start; the version check discards what the snapshot has
  bool bootstrapped = false;
  auto backoff = 200ms;

  while (!stop.stop_requested()) {
    if (!bootstrapped) {
      bootstrapped = bootstrap();
      if (!bootstrapped) {
        interruptible_sleep(stop, backoff);
        backoff = std::min<std::chrono::milliseconds>(backoff * 2, 5s);
        continue;
      }
      backoff = 200ms;
      if (options_.eager_recompute) engine_.current();
    }

    try {
      if (!redis) redis.emplace(redis_options_);
      const auto entries = redis->xread(options_.stream, last_id, 1000ms, 500);
      bool changed = false;
      for (const auto& entry : entries) {
        last_id = entry.id;
        ApplyResult result = ApplyResult::ignored;
        try {
          result = model_.apply(sg::common::from_stream_entry(entry));
        } catch (const std::exception& e) {
          // A malformed event cannot be applied safely; resynchronise rather than diverge.
          log_warn("malformed graph event", {{"stream_id", entry.id}, {"error", e.what()}});
          result = ApplyResult::gap;
        }
        if (result == ApplyResult::applied) {
          changed = true;
          applied_.fetch_add(1);
        } else if (result == ApplyResult::gap) {
          log_warn("graph event gap detected, resynchronising",
                   {{"stream_id", entry.id}, {"applied_version", model_.version()}});
          resyncs_.fetch_add(1);
          bootstrapped = false;
          break;
        }
      }
      if (changed && bootstrapped && options_.eager_recompute) engine_.current();
      backoff = 200ms;
    } catch (const std::exception& e) {
      log_warn("event stream read failed", {{"error", e.what()}});
      redis.reset();
      interruptible_sleep(stop, backoff);
      backoff = std::min<std::chrono::milliseconds>(backoff * 2, 5s);
    }
  }
}

}  // namespace sg::analytics
