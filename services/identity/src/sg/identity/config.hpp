#pragma once

/// @file config.hpp
/// identity-service configuration from environment variables (see README.md for the full list).

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include <sg/common/crypto.hpp>
#include <sg/common/redis.hpp>

namespace sg::identity {

/// In-process limits on top of the gateway's (defence in depth). 0 disables a limit.
struct RateLimitConfig {
  std::chrono::seconds window{60};   ///< RATE_LIMIT_WINDOW_SECONDS
  std::size_t login_per_ip = 30;     ///< LOGIN_RATE_LIMIT_PER_IP: login attempts per client address
  std::size_t login_per_handle = 10; ///< LOGIN_RATE_LIMIT_PER_HANDLE: failed logins per handle
  std::size_t register_per_ip = 20;  ///< REGISTER_RATE_LIMIT_PER_IP: registrations per client address
};

struct Config {
  int port = 8081;                                 ///< IDENTITY_PORT
  std::optional<std::string> database_url;         ///< IDENTITY_DATABASE_URL
  std::optional<std::string> signing_seed_b64;     ///< AUTH_SIGNING_SEED (secret)
  std::chrono::seconds token_ttl{8 * 3600};        ///< AUTH_TOKEN_TTL_SECONDS
  std::string password_hash_profile = "owasp";     ///< PASSWORD_HASH_PROFILE: owasp | fast
  sg::common::PasswordHashParams password_hash = sg::common::PasswordHashParams::owasp();
  sg::common::RedisOptions redis;                  ///< REDIS_HOST / REDIS_PORT
  std::size_t http_threads = 8;                    ///< HTTP_THREADS
  std::size_t db_pool_size = 8;                    ///< DB_POOL_SIZE
  bool seed_on_empty = true;                       ///< SEED_ON_EMPTY
  std::optional<std::string> seed_demo_password;   ///< SEED_DEMO_PASSWORD (secret)
  std::string graph_service_url = "http://localhost:8082";  ///< GRAPH_SERVICE_URL
  std::chrono::seconds taxonomy_refresh{300};      ///< TAXONOMY_REFRESH_SECONDS
  RateLimitConfig rate_limits;

  /// Reads every variable. Required ones are checked where they are needed (require_*), so that
  /// e.g. `migrate` works without signing keys.
  /// @throws std::runtime_error naming the variable when a value is malformed.
  [[nodiscard]] static Config from_env();

  /// @throws std::runtime_error naming the variable when it is unset.
  [[nodiscard]] const std::string& require_database_url() const;
  [[nodiscard]] const std::string& require_signing_seed() const;
};

}  // namespace sg::identity
