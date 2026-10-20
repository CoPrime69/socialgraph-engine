#include "sg/common/outbox.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

#include "sg/common/events.hpp"
#include "sg/common/log.hpp"
#include "sg/common/time.hpp"

namespace sg::common {
namespace {

using namespace std::chrono_literals;

/// Stable 64-bit advisory lock key for a stream (FNV-1a).
std::int64_t derive_lock_key(std::string_view stream) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char c : std::string_view("sg.outbox.relay:")) {
    hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
  }
  for (const char c : stream) {
    hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
  }
  return static_cast<std::int64_t>(hash);
}

std::string bigint_array_literal(const std::vector<std::int64_t>& ids) {
  std::string out = "{";
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) {
      out += ',';
    }
    out += std::to_string(ids[i]);
  }
  out += '}';
  return out;
}

struct OutboxRow {
  std::int64_t id;
  std::string type;
  std::string payload;
  std::int64_t occurred_at_ms;
};

}  // namespace

std::int64_t enqueue_event(pqxx::work& tx, const std::string& type, const nlohmann::json& payload) {
  const auto row = tx.exec_params1("INSERT INTO outbox (type, payload) VALUES ($1, $2::jsonb) RETURNING id", type,
                                   payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
  return row[0].as<std::int64_t>();
}

void enqueue_events(pqxx::work& tx, const std::vector<std::pair<std::string, nlohmann::json>>& events) {
  if (events.empty()) {
    return;
  }
  std::vector<std::string> types;
  std::vector<std::string> payloads;
  types.reserve(events.size());
  payloads.reserve(events.size());
  for (const auto& [type, payload] : events) {
    types.push_back(type);
    payloads.push_back(payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
  }
  // Rows draw their ids from the sequence in the ORDER BY order, so outbox order = vector order.
  tx.exec_params(
      "INSERT INTO outbox (type, payload) SELECT e.type, e.payload::jsonb "
      "FROM unnest($1::text[], $2::text[]) WITH ORDINALITY AS e(type, payload, n) ORDER BY e.n",
      types, payloads);
}

OutboxRelay::OutboxRelay(PgPool& pool, RedisOptions redis, std::string stream, std::chrono::milliseconds poll_interval)
    : pool_(pool),
      redis_options_(std::move(redis)),
      stream_(std::move(stream)),
      poll_interval_(poll_interval.count() > 0 ? poll_interval : 100ms),
      lock_key_(derive_lock_key(stream_)) {}

OutboxRelay::~OutboxRelay() { stop(); }

void OutboxRelay::start() {
  std::lock_guard lock(mutex_);
  if (thread_.joinable()) {
    return;
  }
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
}

void OutboxRelay::stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool OutboxRelay::sleep_for(std::chrono::milliseconds duration) {
  std::unique_lock lock(mutex_);
  return !wake_.wait_for(lock, duration, [this] { return stopping_; });
}

std::size_t OutboxRelay::publish_batch_once(RedisClient& redis) {
  const std::size_t batch_size = batch_size_;
  const auto rows = pool_.with_transaction([batch_size](pqxx::work& tx) {
    std::vector<OutboxRow> out;
    const auto result = tx.exec_params(
        "SELECT id, type, payload::text, (extract(epoch FROM occurred_at) * 1000)::bigint "
        "FROM outbox WHERE published_at IS NULL ORDER BY id LIMIT $1",
        static_cast<std::int64_t>(batch_size));
    out.reserve(result.size());
    for (const auto& row : result) {
      out.push_back({row[0].as<std::int64_t>(), row[1].as<std::string>(), row[2].as<std::string>(),
                     row[3].as<std::int64_t>()});
    }
    return out;
  });
  if (rows.empty()) {
    return 0;
  }

  std::vector<std::int64_t> published;
  published.reserve(rows.size());
  const auto mark_published = [this](const std::vector<std::int64_t>& ids) {
    pool_.with_transaction([&ids](pqxx::work& tx) {
      tx.exec_params("UPDATE outbox SET published_at = now() WHERE id = ANY($1::bigint[])", bigint_array_literal(ids));
    });
  };
  try {
    for (const auto& row : rows) {
      Event event;
      event.event_id = std::to_string(row.id);
      event.type = row.type;
      event.schema = 1;
      event.occurred_at =
          to_rfc3339_millis(std::chrono::system_clock::time_point(std::chrono::milliseconds(row.occurred_at_ms)));
      event.payload = nlohmann::json::parse(row.payload);
      redis.xadd(stream_, to_stream_fields(event), max_len_);
      published.push_back(row.id);
    }
  } catch (...) {
    if (!published.empty()) {
      try {
        mark_published(published);  // keep re-delivery after the failure to a minimum
      } catch (const std::exception& e) {
        log_warn("outbox relay could not mark events published", {{"stream", stream_}, {"error", e.what()}});
      }
    }
    throw;
  }
  mark_published(published);
  published_.fetch_add(published.size());
  log_debug("outbox events published",
            {{"stream", stream_}, {"count", published.size()}, {"last_event_id", published.back()}});
  return published.size();
}

void OutboxRelay::run() {
  std::unique_ptr<pqxx::connection> lock_conn;
  std::optional<RedisClient> redis;
  auto backoff = poll_interval_;
  auto last_lock_check = std::chrono::steady_clock::now();
  std::optional<std::chrono::steady_clock::time_point> last_cleanup;
  const auto follower_retry = std::max<std::chrono::milliseconds>(poll_interval_ * 10, 1s);

  for (;;) {
    {
      std::lock_guard lock(mutex_);
      if (stopping_) {
        break;
      }
    }
    try {
      const auto now = std::chrono::steady_clock::now();
      if (!leader_.load()) {
        if (!lock_conn) {
          lock_conn = pool_.open_dedicated_connection();
        }
        pqxx::nontransaction tx{*lock_conn};
        const bool acquired = tx.exec_params1("SELECT pg_try_advisory_lock($1)", lock_key_)[0].as<bool>();
        if (!acquired) {
          if (!sleep_for(follower_retry)) {
            break;
          }
          continue;
        }
        leader_.store(true);
        last_lock_check = now;
        log_info("outbox relay is the leader", {{"stream", stream_}});
      } else if (now - last_lock_check >= 1s) {
        // The session lock lives as long as this connection; make sure it still does.
        pqxx::nontransaction tx{*lock_conn};
        tx.exec("SELECT 1");
        last_lock_check = now;
      }

      if (!redis) {
        redis.emplace(redis_options_);
      }
      const std::size_t published = publish_batch_once(*redis);

      if (retention_.count() > 0 && (!last_cleanup || now - *last_cleanup >= 1h)) {
        last_cleanup = now;
        const auto deleted = pool_.with_transaction([this](pqxx::work& tx) {
          return tx
              .exec_params("DELETE FROM outbox WHERE published_at IS NOT NULL AND published_at < now() - make_interval(secs => $1)",
                           static_cast<double>(retention_.count()))
              .affected_rows();
        });
        if (deleted > 0) {
          log_info("outbox cleanup", {{"stream", stream_}, {"deleted", deleted}});
        }
      }

      backoff = poll_interval_;
      if (published < batch_size_ && !sleep_for(poll_interval_)) {
        break;
      }
    } catch (const std::exception& e) {
      log_warn("outbox relay error, retrying", {{"stream", stream_}, {"error", e.what()}, {"retry_in_ms", backoff.count()}});
      redis.reset();
      if (lock_conn) {
        bool healthy = false;
        try {
          pqxx::nontransaction tx{*lock_conn};
          tx.exec("SELECT 1");
          healthy = true;
        } catch (const std::exception&) {
        }
        if (!healthy) {
          lock_conn.reset();
          if (leader_.exchange(false)) {
            log_warn("outbox relay lost leadership", {{"stream", stream_}});
          }
        }
      }
      if (!sleep_for(backoff)) {
        break;
      }
      backoff = std::min<std::chrono::milliseconds>(backoff * 2, 5s);
    }
  }

  if (lock_conn && leader_.load()) {
    try {
      pqxx::nontransaction tx{*lock_conn};
      tx.exec_params("SELECT pg_advisory_unlock($1)", lock_key_);
    } catch (const std::exception&) {
      // Closing the connection releases the lock anyway.
    }
  }
  lock_conn.reset();
  leader_.store(false);
}

}  // namespace sg::common
