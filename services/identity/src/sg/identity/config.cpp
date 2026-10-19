#include "sg/identity/config.hpp"

#include <stdexcept>

#include <sg/common/config.hpp>

namespace sg::identity {
namespace {

std::size_t env_size(const char* name, long long fallback, long long min, long long max) {
  return static_cast<std::size_t>(sg::common::env_int(name, fallback, min, max));
}

}  // namespace

Config Config::from_env() {
  using sg::common::env_bool;
  using sg::common::env_get;
  using sg::common::env_int;
  using sg::common::env_or;

  Config c;
  c.port = static_cast<int>(env_int("IDENTITY_PORT", 8081, 1, 65535));
  c.database_url = env_get("IDENTITY_DATABASE_URL");
  c.signing_seed_b64 = env_get("AUTH_SIGNING_SEED");
  c.token_ttl = std::chrono::seconds(env_int("AUTH_TOKEN_TTL_SECONDS", 8 * 3600, 60, 30LL * 24 * 3600));

  c.password_hash_profile = env_or("PASSWORD_HASH_PROFILE", "owasp");
  if (c.password_hash_profile == "owasp") {
    c.password_hash = sg::common::PasswordHashParams::owasp();
  } else if (c.password_hash_profile == "fast") {
    c.password_hash = sg::common::PasswordHashParams::fast_for_tests();
  } else {
    throw std::runtime_error("PASSWORD_HASH_PROFILE must be 'owasp' or 'fast'");
  }

  c.redis.host = env_or("REDIS_HOST", "127.0.0.1");
  c.redis.port = static_cast<int>(env_int("REDIS_PORT", 6379, 1, 65535));
  c.http_threads = env_size("HTTP_THREADS", 8, 1, 512);
  c.db_pool_size = env_size("DB_POOL_SIZE", 8, 1, 256);
  c.seed_on_empty = env_bool("SEED_ON_EMPTY", true);
  c.seed_demo_password = env_get("SEED_DEMO_PASSWORD");
  c.graph_service_url = env_or("GRAPH_SERVICE_URL", "http://localhost:8082");
  c.taxonomy_refresh = std::chrono::seconds(env_int("TAXONOMY_REFRESH_SECONDS", 300, 5, 24 * 3600));

  c.rate_limits.window = std::chrono::seconds(env_int("RATE_LIMIT_WINDOW_SECONDS", 60, 1, 24 * 3600));
  c.rate_limits.login_per_ip = env_size("LOGIN_RATE_LIMIT_PER_IP", 30, 0, 1'000'000);
  c.rate_limits.login_per_handle = env_size("LOGIN_RATE_LIMIT_PER_HANDLE", 10, 0, 1'000'000);
  c.rate_limits.register_per_ip = env_size("REGISTER_RATE_LIMIT_PER_IP", 20, 0, 1'000'000);
  return c;
}

const std::string& Config::require_database_url() const {
  if (!database_url) {
    throw std::runtime_error("environment variable IDENTITY_DATABASE_URL is required");
  }
  return *database_url;
}

const std::string& Config::require_signing_seed() const {
  if (!signing_seed_b64) {
    throw std::runtime_error("environment variable AUTH_SIGNING_SEED is required (generate one with: identity-service keygen)");
  }
  return *signing_seed_b64;
}

}  // namespace sg::identity
