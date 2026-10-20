#include "sg/graph_service/infra/identity_events_consumer.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <utility>

#include <nlohmann/json.hpp>

#include <sg/common/errors.hpp>
#include <sg/common/events.hpp>
#include <sg/common/log.hpp>
#include <sg/common/pg.hpp>

namespace sg::graph_service::infra {
namespace {

using sg::common::StreamEntry;

/// Database or Redis unreachable, pool exhausted, serialization failure: retry, never dead-letter.
bool is_transient(const std::exception& e) noexcept {
  if (sg::common::is_connection_error(e)) {
    return true;
  }
  if (const auto* api = dynamic_cast<const sg::common::ApiError*>(&e); api != nullptr && api->status() == 503) {
    return true;
  }
  return dynamic_cast<const pqxx::transaction_rollback*>(&e) != nullptr ||
         dynamic_cast<const sg::common::RedisError*>(&e) != nullptr;
}

nlohmann::json fields_json(const StreamEntry& entry) {
  nlohmann::json out = nlohmann::json::object();
  for (const auto& [k, v] : entry.fields) {
    out[k] = v;
  }
  return out;
}

}  // namespace

std::string default_consumer_name() {
  std::array<char, 256> host{};
  if (::gethostname(host.data(), host.size() - 1) == 0 && host[0] != '\0') {
    return std::string(host.data());
  }
  return "graph-service";
}

IdentityEventsConsumer::IdentityEventsConsumer(app::GraphService& service, sg::common::RedisOptions redis,
                                               Options options)
    : service_(service), redis_options_(std::move(redis)), options_(std::move(options)) {
  if (options_.consumer.empty()) {
    options_.consumer = default_consumer_name();
  }
  options_.batch_size = std::max<std::size_t>(options_.batch_size, 1);
  options_.max_attempts = std::max(options_.max_attempts, 1);
}

IdentityEventsConsumer::~IdentityEventsConsumer() { stop(); }

void IdentityEventsConsumer::start() {
  std::lock_guard lock(mutex_);
  if (thread_.joinable()) {
    return;
  }
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
}

void IdentityEventsConsumer::stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool IdentityEventsConsumer::sleep_for(std::chrono::milliseconds duration) {
  std::unique_lock lock(mutex_);
  return !wake_.wait_for(lock, duration, [this] { return stopping_; });
}

bool IdentityEventsConsumer::dead_letter(sg::common::RedisClient& redis, const StreamEntry& entry,
                                         const std::string& error) {
  try {
    service_.record_dead_letter(options_.stream, entry.id, fields_json(entry), error);
  } catch (const std::exception& e) {
    sg::common::log_warn("dead-lettering failed, will retry", {{"entry_id", entry.id}, {"error", e.what()}});
    return false;
  }
  redis.xack(options_.stream, options_.group, entry.id);
  attempts_.erase(entry.id);
  dead_lettered_.fetch_add(1);
  sg::common::log_error("event moved to dead_letter_events",
                        {{"stream", options_.stream}, {"entry_id", entry.id}, {"error", error}});
  return true;
}

bool IdentityEventsConsumer::handle(sg::common::RedisClient& redis, const StreamEntry& entry) {
  try {
    const sg::common::Event event = sg::common::from_stream_entry(entry);
    const auto outcome = service_.handle_identity_event(event, options_.stream);
    redis.xack(options_.stream, options_.group, entry.id);
    attempts_.erase(entry.id);
    if (outcome == app::EventOutcome::applied) {
      applied_.fetch_add(1);
    } else {
      sg::common::log_debug("event skipped", {{"entry_id", entry.id},
                                              {"type", event.type},
                                              {"duplicate", outcome == app::EventOutcome::duplicate}});
    }
    return true;
  } catch (const sg::common::RedisError&) {
    throw;  // connection problem: handled by the caller
  } catch (const app::PoisonEvent& e) {
    return dead_letter(redis, entry, e.what());
  } catch (const std::invalid_argument& e) {  // malformed envelope (from_stream_entry)
    return dead_letter(redis, entry, e.what());
  } catch (const std::exception& e) {
    if (is_transient(e)) {
      return false;
    }
    const int attempts = ++attempts_[entry.id];
    if (attempts >= options_.max_attempts) {
      return dead_letter(redis, entry, std::string("failed ") + std::to_string(attempts) + " times: " + e.what());
    }
    sg::common::log_warn("event handling failed, will retry",
                         {{"entry_id", entry.id}, {"attempt", attempts}, {"error", e.what()}});
    return false;
  }
}

IdentityEventsConsumer::BatchResult IdentityEventsConsumer::handle_batch(sg::common::RedisClient& redis,
                                                                         const std::vector<StreamEntry>& entries) {
  BatchResult result;
  for (const auto& entry : entries) {
    if (handle(redis, entry)) {
      ++result.acknowledged;
    } else {
      // Keep stream order: stop at the first entry that must be retried.
      result.retry_later = true;
      break;
    }
  }
  return result;
}

IdentityEventsConsumer::BatchResult IdentityEventsConsumer::claim_stale(sg::common::RedisClient& redis) {
  BatchResult total;
  std::string start = "0-0";
  for (int page = 0; page < 10; ++page) {
    auto claimed = redis.xautoclaim(options_.stream, options_.group, options_.consumer, options_.claim_min_idle, start,
                                    options_.batch_size);
    if (!claimed.entries.empty()) {
      sg::common::log_info("claimed stale pending events", {{"count", claimed.entries.size()}});
    }
    const auto r = handle_batch(redis, claimed.entries);
    total.acknowledged += r.acknowledged;
    if (r.retry_later) {
      total.retry_later = true;
      break;
    }
    if (claimed.next_start_id == "0-0") {
      break;
    }
    start = claimed.next_start_id;
  }
  return total;
}

std::size_t IdentityEventsConsumer::poll_once(sg::common::RedisClient& redis, std::chrono::milliseconds block) {
  redis.xgroup_create(options_.stream, options_.group, "0");
  std::size_t acknowledged = 0;
  for (;;) {  // own pending entries first
    const auto pending = redis.xreadgroup(options_.stream, options_.group, options_.consumer,
                                          std::chrono::milliseconds(0), options_.batch_size, "0");
    const auto r = handle_batch(redis, pending);
    acknowledged += r.acknowledged;
    if (pending.empty() || r.retry_later) {
      if (r.retry_later) {
        return acknowledged;
      }
      break;
    }
  }
  const auto claimed = claim_stale(redis);
  acknowledged += claimed.acknowledged;
  if (claimed.retry_later) {
    return acknowledged;
  }
  const auto fresh =
      redis.xreadgroup(options_.stream, options_.group, options_.consumer, block, options_.batch_size, ">");
  acknowledged += handle_batch(redis, fresh).acknowledged;
  return acknowledged;
}

void IdentityEventsConsumer::run() {
  sg::common::log_info("identity events consumer started", {{"stream", options_.stream},
                                                             {"group", options_.group},
                                                             {"consumer", options_.consumer}});
  sg::common::RedisClient redis(redis_options_);
  bool group_ready = false;
  bool read_pending = true;  // crash recovery: entries delivered to us but never acknowledged
  auto next_claim = std::chrono::steady_clock::now();
  auto backoff = std::chrono::milliseconds(100);
  auto last_error_log = std::chrono::steady_clock::time_point{};
  for (;;) {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) {
        break;
      }
    }
    try {
      if (!group_ready) {
        redis.xgroup_create(options_.stream, options_.group, "0");
        group_ready = true;
      }
      BatchResult result;
      if (std::chrono::steady_clock::now() >= next_claim) {
        result = claim_stale(redis);
        next_claim = std::chrono::steady_clock::now() + options_.claim_interval;
      }
      if (!result.retry_later) {
        if (read_pending) {
          const auto pending = redis.xreadgroup(options_.stream, options_.group, options_.consumer,
                                                std::chrono::milliseconds(0), options_.batch_size, "0");
          result = handle_batch(redis, pending);
          read_pending = !pending.empty() || result.retry_later;
        } else {
          const auto fresh = redis.xreadgroup(options_.stream, options_.group, options_.consumer, options_.block,
                                              options_.batch_size, ">");
          result = handle_batch(redis, fresh);
        }
      }
      if (result.retry_later) {
        read_pending = true;  // the failed entry is pending: retry it before newer ones
        if (!sleep_for(backoff)) {
          break;
        }
        backoff = std::min(backoff * 2, std::chrono::milliseconds(5000));
      } else {
        backoff = std::chrono::milliseconds(100);
      }
    } catch (const std::exception& e) {
      const auto now = std::chrono::steady_clock::now();
      if (now - last_error_log >= std::chrono::seconds(30)) {
        last_error_log = now;
        sg::common::log_warn("identity events consumer error, retrying", {{"error", e.what()}});
      }
      group_ready = false;  // the stream or group may have been deleted
      read_pending = true;
      if (!sleep_for(backoff)) {
        break;
      }
      backoff = std::min(backoff * 2, std::chrono::milliseconds(5000));
    }
  }
  sg::common::log_info("identity events consumer stopped", {{"applied", applied_.load()}});
}

}  // namespace sg::graph_service::infra
