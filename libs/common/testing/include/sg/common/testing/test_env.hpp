#pragma once

/// @file test_env.hpp
/// Helpers for integration tests that need PostgreSQL or Redis. They read SG_TEST_DATABASE_URL
/// and SG_TEST_REDIS_HOST / SG_TEST_REDIS_PORT; tests call GTEST_SKIP() when these are unset, so
/// the suites run anywhere. Each test gets its own throw-away database (the role needs CREATEDB)
/// and unique stream names, so tests never interfere with each other or with running services.

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "sg/common/pqxx.hpp"

#include "sg/common/config.hpp"
#include "sg/common/crypto.hpp"
#include "sg/common/redis.hpp"

namespace sg::common::testing {

/// SG_TEST_DATABASE_URL, or std::nullopt when unset.
inline std::optional<std::string> test_database_url() { return env_get("SG_TEST_DATABASE_URL"); }

/// Redis from SG_TEST_REDIS_HOST (+ SG_TEST_REDIS_PORT, default 6379), or std::nullopt when unset.
inline std::optional<RedisOptions> test_redis_options() {
  const auto host = env_get("SG_TEST_REDIS_HOST");
  if (!host) {
    return std::nullopt;
  }
  RedisOptions options;
  options.host = *host;
  options.port = static_cast<int>(env_int("SG_TEST_REDIS_PORT", 6379, 1, 65535));
  return options;
}

/// `prefix` + "." + 12 random hex characters (unique stream / group names).
inline std::string unique_name(std::string_view prefix) { return std::string(prefix) + "." + random_hex(6); }

/// Replaces the database of a libpq URI (postgresql://u:p@h:port/db?x=y) or key=value conninfo.
inline std::string with_database(const std::string& conninfo, const std::string& dbname) {
  const auto scheme = conninfo.find("://");
  if (scheme == std::string::npos) {
    return conninfo + " dbname=" + dbname;  // the last occurrence of a keyword wins in libpq
  }
  const auto authority_end = conninfo.find_first_of("/?", scheme + 3);
  if (authority_end == std::string::npos) {
    return conninfo + "/" + dbname;
  }
  if (conninfo[authority_end] == '?') {
    return conninfo.substr(0, authority_end) + "/" + dbname + conninfo.substr(authority_end);
  }
  const auto path_end = conninfo.find('?', authority_end);
  return conninfo.substr(0, authority_end + 1) + dbname +
         (path_end == std::string::npos ? std::string() : conninfo.substr(path_end));
}

/// A database created on construction and dropped (WITH (FORCE)) on destruction.
class TempDatabase {
 public:
  explicit TempDatabase(std::string admin_url, std::string_view prefix = "sg_test")
      : admin_url_(std::move(admin_url)), name_(std::string(prefix) + "_" + random_hex(6)) {
    pqxx::connection conn{admin_url_};
    pqxx::nontransaction tx{conn};
    tx.exec("CREATE DATABASE " + conn.quote_name(name_));
    url_ = with_database(admin_url_, name_);
  }
  ~TempDatabase() {
    try {
      pqxx::connection conn{admin_url_};
      pqxx::nontransaction tx{conn};
      tx.exec("DROP DATABASE IF EXISTS " + conn.quote_name(name_) + " WITH (FORCE)");
    } catch (const std::exception&) {
      // Best effort: a leftover test database is harmless.
    }
  }
  TempDatabase(const TempDatabase&) = delete;
  TempDatabase& operator=(const TempDatabase&) = delete;

  [[nodiscard]] const std::string& url() const noexcept { return url_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }

 private:
  std::string admin_url_;
  std::string name_;
  std::string url_;
};

}  // namespace sg::common::testing
