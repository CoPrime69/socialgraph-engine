#pragma once

/// @file config.hpp
/// graph-service configuration from environment variables (see README.md for the full list).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <sg/common/redis.hpp>

namespace sg::graph_service {

struct Config {
  int port = 8082;                             ///< GRAPH_PORT
  std::optional<std::string> database_url;     ///< GRAPH_DATABASE_URL
  std::optional<std::string> public_key_b64;   ///< AUTH_PUBLIC_KEY
  sg::common::RedisOptions redis;              ///< REDIS_HOST / REDIS_PORT
  std::size_t http_threads = 8;                ///< HTTP_THREADS
  std::size_t db_pool_size = 8;                ///< DB_POOL_SIZE
  bool seed_on_empty = true;                   ///< SEED_ON_EMPTY
  std::string seed_dir = "data/stackoverflow"; ///< SEED_DIR
  std::string consumer_name;                   ///< CONSUMER_NAME (default: host name)
  std::size_t cache_entries = 100'000;         ///< GRAPH_CACHE_ENTRIES (0 disables the read cache)
  std::int64_t search_cache_ttl_ms = 2000;     ///< GRAPH_SEARCH_CACHE_TTL_MS (0 disables search caching)

  /// @throws std::runtime_error naming the variable when a value is malformed.
  [[nodiscard]] static Config from_env();

  /// @throws std::runtime_error naming the variable when it is unset.
  [[nodiscard]] const std::string& require_database_url() const;
  [[nodiscard]] const std::string& require_public_key() const;
};

}  // namespace sg::graph_service
