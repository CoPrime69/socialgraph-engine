#include "sg/common/pg.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

#include "sg/common/errors.hpp"
#include "sg/common/log.hpp"

namespace sg::common {
namespace {

bool is_uri(std::string_view conninfo) {
  return conninfo.rfind("postgresql://", 0) == 0 || conninfo.rfind("postgres://", 0) == 0;
}

/// Adds connect_timeout unless the caller already set it.
std::string with_connect_timeout(const std::string& conninfo, int seconds) {
  if (seconds <= 0 || conninfo.find("connect_timeout") != std::string::npos) {
    return conninfo;
  }
  if (is_uri(conninfo)) {
    return conninfo + (conninfo.find('?') == std::string::npos ? "?" : "&") + "connect_timeout=" +
           std::to_string(seconds);
  }
  return conninfo + (conninfo.empty() ? "" : " ") + "connect_timeout=" + std::to_string(seconds);
}

/// Removes the password from a conninfo for log messages.
std::string redact(std::string text) {
  if (is_uri(text)) {
    const auto scheme_end = text.find("://") + 3;
    const auto at = text.find('@', scheme_end);
    const auto colon = text.find(':', scheme_end);
    if (at != std::string::npos && colon != std::string::npos && colon < at) {
      text.replace(colon + 1, at - colon - 1, "***");
    }
    return text;
  }
  const auto pos = text.find("password=");
  if (pos != std::string::npos) {
    const auto end = text.find(' ', pos);
    text.replace(pos + 9, (end == std::string::npos ? text.size() : end) - pos - 9, "***");
  }
  return text;
}

void configure(pqxx::connection& conn, const PgPoolOptions& options) {
  pqxx::nontransaction tx{conn};
  tx.exec_params("SELECT set_config('statement_timeout', $1, false), "
                 "set_config('idle_in_transaction_session_timeout', $2, false), "
                 "set_config('application_name', $3, false)",
                 std::to_string(std::max(options.statement_timeout_ms, 0)),
                 std::to_string(std::max(options.statement_timeout_ms * 6, 60000)), options.application_name);
}

}  // namespace

PgPool::PgPool(PgPoolOptions options)
    : options_(std::move(options)), conninfo_(with_connect_timeout(options_.conninfo, options_.connect_timeout_s)) {
  if (options_.size == 0) {
    throw std::invalid_argument("PgPool size must be at least 1");
  }
  idle_.reserve(options_.size);
}

PgPool::~PgPool() = default;

std::unique_ptr<pqxx::connection> PgPool::open_dedicated_connection() const {
  auto conn = std::make_unique<pqxx::connection>(conninfo_);
  configure(*conn, options_);
  return conn;
}

PgPool::Lease PgPool::acquire() {
  std::unique_lock lock(mutex_);
  const bool got_slot = available_.wait_for(lock, options_.acquire_timeout, [this] {
    return !idle_.empty() || idle_.size() + in_use_ < options_.size;
  });
  if (!got_slot) {
    ++timeouts_;
    throw unavailable("Database connection pool exhausted");
  }
  ++in_use_;
  ++acquisitions_;
  std::unique_ptr<pqxx::connection> conn;
  if (!idle_.empty()) {
    conn = std::move(idle_.back());
    idle_.pop_back();
  }
  lock.unlock();

  if (conn && conn->is_open()) {
    return Lease(this, std::move(conn));
  }
  conn.reset();
  try {
    conn = open_dedicated_connection();
  } catch (const std::exception& e) {
    give_back(nullptr, true);
    log_warn("database connection failed", {{"conninfo", redact(options_.conninfo)}, {"error", e.what()}});
    throw unavailable("Database unavailable");
  }
  return Lease(this, std::move(conn));
}

void PgPool::give_back(std::unique_ptr<pqxx::connection> conn, bool discard) noexcept {
  {
    std::lock_guard lock(mutex_);
    --in_use_;
    if (conn && !discard && conn->is_open()) {
      idle_.push_back(std::move(conn));
    }
  }
  available_.notify_one();
  // A discarded connection (if any) is closed here, outside the lock.
}

bool PgPool::ping() {
  try {
    auto lease = acquire();
    try {
      pqxx::nontransaction tx{*lease};
      tx.exec("SELECT 1");
      return true;
    } catch (const std::exception&) {
      lease.discard();
      return false;
    }
  } catch (const std::exception&) {
    return false;
  }
}

PgPool::Stats PgPool::stats() const {
  std::lock_guard lock(mutex_);
  return {options_.size, idle_.size(), in_use_, acquisitions_, timeouts_};
}

PgPool::Lease::Lease(PgPool* pool, std::unique_ptr<pqxx::connection> conn) noexcept
    : pool_(pool), conn_(std::move(conn)) {}

PgPool::Lease::Lease(Lease&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)), conn_(std::move(other.conn_)), discard_(other.discard_) {}

PgPool::Lease& PgPool::Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    release();
    pool_ = std::exchange(other.pool_, nullptr);
    conn_ = std::move(other.conn_);
    discard_ = other.discard_;
  }
  return *this;
}

PgPool::Lease::~Lease() { release(); }

void PgPool::Lease::release() noexcept {
  if (pool_ != nullptr) {
    pool_->give_back(std::move(conn_), discard_);
    pool_ = nullptr;
  }
}

void run_migrations(PgPool& pool, std::span<const Migration> migrations, std::int64_t advisory_lock_key) {
  for (std::size_t i = 0; i < migrations.size(); ++i) {
    if (migrations[i].version <= 0 || migrations[i].name.empty()) {
      throw std::invalid_argument("invalid migration at index " + std::to_string(i));
    }
    if (i > 0 && migrations[i].version <= migrations[i - 1].version) {
      throw std::invalid_argument("migrations must have strictly increasing versions");
    }
  }

  constexpr const char* kCreateTable =
      "CREATE TABLE IF NOT EXISTS schema_migrations ("
      " version integer PRIMARY KEY,"
      " name text NOT NULL,"
      " applied_at timestamptz NOT NULL DEFAULT now())";

  // Verify the recorded history against the binary before changing anything.
  const auto applied = pool.with_transaction([&](pqxx::work& tx) {
    tx.exec_params("SELECT pg_advisory_xact_lock($1)", advisory_lock_key);
    tx.exec(kCreateTable);
    std::map<int, std::string> recorded;
    for (const auto& row : tx.exec("SELECT version, name FROM schema_migrations ORDER BY version")) {
      recorded.emplace(row[0].as<int>(), row[1].as<std::string>());
    }
    return recorded;
  });
  for (const auto& m : migrations) {
    const auto it = applied.find(m.version);
    if (it != applied.end() && it->second != m.name) {
      throw std::runtime_error("migration " + std::to_string(m.version) + " is recorded as '" + it->second +
                               "' but this binary has '" + m.name + "'");
    }
  }
  if (!applied.empty() && (migrations.empty() || applied.rbegin()->first > migrations.back().version)) {
    log_warn("database schema is newer than this binary", {{"database_version", applied.rbegin()->first}});
  }

  for (const auto& m : migrations) {
    if (applied.contains(m.version)) {
      continue;
    }
    const bool newly_applied = pool.with_transaction([&](pqxx::work& tx) {
      tx.exec_params("SELECT pg_advisory_xact_lock($1)", advisory_lock_key);
      // Another instance may have applied it while we waited for the lock.
      const auto existing = tx.exec_params("SELECT name FROM schema_migrations WHERE version = $1", m.version);
      if (!existing.empty()) {
        if (existing[0][0].as<std::string>() != m.name) {
          throw std::runtime_error("migration " + std::to_string(m.version) + " name mismatch");
        }
        return false;
      }
      tx.exec("SET LOCAL statement_timeout = 0");
      tx.exec(m.sql);
      tx.exec_params("INSERT INTO schema_migrations (version, name) VALUES ($1, $2)", m.version, m.name);
      return true;
    });
    if (newly_applied) {
      log_info("applied migration", {{"version", m.version}, {"name", m.name}});
    }
  }
}

void vacuum_analyze(PgPool& pool, const std::string& tables) {
  try {
    pool.with_read([&](pqxx::nontransaction& tx) {
      // VACUUM cannot run in a transaction block; lift the pooled session's statement timeout for
      // it and put the previous value back afterwards.
      const auto previous = tx.query_value<std::string>("SELECT current_setting('statement_timeout')");
      tx.exec("SET statement_timeout = 0");
      try {
        tx.exec("VACUUM (ANALYZE) " + tables);
      } catch (...) {
        tx.exec_params("SELECT set_config('statement_timeout', $1, false)", previous);
        throw;
      }
      tx.exec_params("SELECT set_config('statement_timeout', $1, false)", previous);
    });
  } catch (const std::exception& e) {
    log_warn("vacuum after import failed", {{"tables", tables}, {"error", e.what()}});
  }
}

bool is_connection_error(const std::exception& e) noexcept {
  if (dynamic_cast<const pqxx::broken_connection*>(&e) != nullptr ||
      dynamic_cast<const pqxx::in_doubt_error*>(&e) != nullptr) {
    return true;
  }
  if (const auto* api = dynamic_cast<const ApiError*>(&e); api != nullptr) {
    return api->status() == 503;
  }
  if (const auto* sql = dynamic_cast<const pqxx::sql_error*>(&e); sql != nullptr) {
    const std::string& state = sql->sqlstate();
    // 08: connection exception, 57: operator intervention (shutdown), 53: insufficient resources.
    return state.rfind("08", 0) == 0 || state.rfind("57", 0) == 0 || state.rfind("53", 0) == 0;
  }
  return false;
}

}  // namespace sg::common
