#include "sg/graph_service/infra/graph_events_invalidator.hpp"

#include <exception>
#include <utility>

#include <nlohmann/json.hpp>

#include <sg/common/events.hpp>
#include <sg/common/log.hpp>

namespace sg::graph_service::infra {

GraphEventsInvalidator::GraphEventsInvalidator(http::ReadCache& cache, sg::common::RedisOptions redis,
                                               Options options)
    : cache_(cache), redis_options_(std::move(redis)), options_(std::move(options)) {}

GraphEventsInvalidator::~GraphEventsInvalidator() { stop(); }

void GraphEventsInvalidator::start() {
  if (!thread_.joinable()) {
    thread_ = std::jthread([this](const std::stop_token& stop) { run(stop); });
  }
}

void GraphEventsInvalidator::stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
}

int GraphEventsInvalidator::apply(http::ReadCache& cache, const sg::common::StreamEntry& entry) {
  sg::common::Event event;
  try {
    event = sg::common::from_stream_entry(entry);
  } catch (const std::exception&) {
    return 0;  // not ours to judge; the analytics consumer dead-letters malformed entries
  }
  const auto id = [&](const nlohmann::json& object, const char* field) -> std::int64_t {
    const auto it = object.find(field);
    return it != object.end() && it->is_number_integer() ? it->get<std::int64_t>() : 0;
  };
  int invalidated = 0;
  const auto drop = [&](std::int64_t member) {
    if (member > 0) {
      cache.invalidate(member);
      ++invalidated;
    }
  };
  if (event.type == "connection.added" || event.type == "connection.removed") {
    drop(id(event.payload, "user_a"));
    drop(id(event.payload, "user_b"));
  } else if (event.type == "user.created") {
    if (const auto user = event.payload.find("user"); user != event.payload.end() && user->is_object()) {
      drop(id(*user, "id"));
    }
  }
  return invalidated;
}

void GraphEventsInvalidator::run(const std::stop_token& stop) {
  using namespace std::chrono;
  const auto since = duration_cast<milliseconds>(system_clock::now().time_since_epoch()) - options_.start_lookback;
  std::string last_id = std::to_string(since.count()) + "-0";
  sg::common::RedisClient redis(redis_options_);
  bool failing = false;
  while (!stop.stop_requested()) {
    try {
      const auto entries = redis.xread(options_.stream, last_id, options_.block, 1000);
      if (failing) {
        cache_.clear();  // events may have been missed while Redis was unreachable
        failing = false;
        sg::common::log_info("graph.events invalidation resumed; read cache cleared");
      }
      for (const auto& entry : entries) {
        apply(cache_, entry);
        last_id = entry.id;
      }
    } catch (const std::exception& e) {
      if (!failing) {
        sg::common::log_warn("graph.events invalidation failing", {{"error", e.what()}});
        failing = true;
      }
      cache_.clear();
      std::this_thread::sleep_for(500ms);
    }
  }
}

}  // namespace sg::graph_service::infra
