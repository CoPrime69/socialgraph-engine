#pragma once

/// @file pg.hpp
/// PostgreSQL connection pool (libpqxx) and schema migrations.
///
/// All SQL issued with user-supplied values must be parameterized (`exec_params`/prepared
/// statements); never concatenate values into SQL text.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "sg/common/pqxx.hpp"

#include "sg/common/migration.hpp"

namespace sg::common {

struct PgPoolOptions {
  std::string conninfo;  ///< libpq URI or key=value string
  std::size_t size = 8;  ///< maximum number of open connections
  std::chrono::milliseconds acquire_timeout{2000};
  int statement_timeout_ms = 5000;  ///< 0 disables
  int connect_timeout_s = 5;
  std::string application_name = "socialgraph";  ///< shown in pg_stat_activity
};

/// Fixed-size, thread-safe pool of libpqxx connections.
///
/// Connections are opened lazily on demand and configured with statement_timeout,
/// idle_in_transaction_session_timeout and application_name. A connection that is found closed,
/// or that failed with a connection error, is discarded instead of being returned, and the slot is
/// re-opened on a later acquire(). The pool must outlive every Lease.
class PgPool {
 public:
  explicit PgPool(PgPoolOptions options);
  ~PgPool();
  PgPool(const PgPool&) = delete;
  PgPool& operator=(const PgPool&) = delete;

  /// Exclusive use of one connection; returns it to the pool on destruction. Finish (commit or
  /// destroy) any transaction on it before the lease goes away.
  class Lease {
   public:
    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    ~Lease();

    [[nodiscard]] pqxx::connection& operator*() { return *conn_; }
    [[nodiscard]] pqxx::connection* operator->() { return conn_.get(); }
    /// Marks the connection as broken: it is closed instead of being returned to the pool.
    void discard() noexcept { discard_ = true; }

   private:
    friend class PgPool;
    Lease(PgPool* pool, std::unique_ptr<pqxx::connection> conn) noexcept;
    void release() noexcept;

    PgPool* pool_ = nullptr;
    std::unique_ptr<pqxx::connection> conn_;
    bool discard_ = false;
  };

  /// Waits up to acquire_timeout for a connection.
  /// @throws ApiError 503 unavailable on timeout or when the database cannot be reached.
  [[nodiscard]] Lease acquire();

  /// Runs `f(pqxx::work&)` in a read-committed transaction on a pooled connection; commits when
  /// `f` returns, rolls back and rethrows when it throws. Returns what `f` returns.
  template <class F>
  auto with_transaction(F&& f);

  /// Opens a new connection that is not part of the pool (e.g. to hold a session-level advisory
  /// lock), configured like pooled connections. @throws pqxx::broken_connection.
  [[nodiscard]] std::unique_ptr<pqxx::connection> open_dedicated_connection() const;

  /// True when a connection can be acquired and answers `SELECT 1`.
  [[nodiscard]] bool ping();

  struct Stats {
    std::size_t size;
    std::size_t idle;
    std::size_t in_use;
    std::uint64_t acquisitions;
    std::uint64_t timeouts;
  };
  [[nodiscard]] Stats stats() const;

  [[nodiscard]] const PgPoolOptions& options() const noexcept { return options_; }

 private:
  void give_back(std::unique_ptr<pqxx::connection> conn, bool discard) noexcept;

  PgPoolOptions options_;
  std::string conninfo_;  // options_.conninfo with connect_timeout applied
  mutable std::mutex mutex_;
  std::condition_variable available_;
  std::vector<std::unique_ptr<pqxx::connection>> idle_;
  std::size_t in_use_ = 0;
  std::uint64_t acquisitions_ = 0;
  std::uint64_t timeouts_ = 0;
};

template <class F>
auto PgPool::with_transaction(F&& f) {
  // A pooled connection may have died while idle (database restart); that is only noticed when
  // BEGIN is sent. Nothing has run yet at that point, so it is safe to retry once on a new one.
  for (int attempt = 1;; ++attempt) {
    Lease lease = acquire();
    std::optional<pqxx::work> tx;
    try {
      tx.emplace(*lease);
    } catch (const pqxx::broken_connection&) {
      lease.discard();
      if (attempt < 2) {
        continue;
      }
      throw;
    }
    try {
      if constexpr (std::is_void_v<std::invoke_result_t<F&, pqxx::work&>>) {
        std::invoke(f, *tx);
        tx->commit();
        return;
      } else {
        auto result = std::invoke(f, *tx);
        tx->commit();
        return result;
      }
    } catch (const pqxx::broken_connection&) {
      tx.reset();
      lease.discard();
      throw;
    } catch (const pqxx::in_doubt_error&) {
      tx.reset();
      lease.discard();
      throw;
    }
  }
}

/// Applies `migrations` (strictly increasing versions) that are not yet recorded in the
/// `schema_migrations` table, each in its own transaction under `pg_advisory_xact_lock(key)`, so
/// concurrent instances never apply a migration twice. Idempotent.
/// @throws std::runtime_error when an applied migration's recorded name differs from the binary's
///         (the history was rewritten), or when a migration fails (its transaction is rolled back).
void run_migrations(PgPool& pool, std::span<const Migration> migrations, std::int64_t advisory_lock_key);

/// True when `e` (or its cause) means the database is unreachable rather than a query failing.
[[nodiscard]] bool is_connection_error(const std::exception& e) noexcept;

}  // namespace sg::common
