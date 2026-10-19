#include "sg/graph_service/config.hpp"

#include <stdexcept>

#include <sg/common/config.hpp>

#include "sg/graph_service/infra/identity_events_consumer.hpp"

namespace sg::graph_service {

Config Config::from_env() {
  using sg::common::env_bool;
  using sg::common::env_get;
  using sg::common::env_int;
  using sg::common::env_or;

  Config c;
  c.port = static_cast<int>(env_int("GRAPH_PORT", 8082, 1, 65535));
  c.database_url = env_get("GRAPH_DATABASE_URL");
  c.public_key_b64 = env_get("AUTH_PUBLIC_KEY");
  c.redis.host = env_or("REDIS_HOST", "127.0.0.1");
  c.redis.port = static_cast<int>(env_int("REDIS_PORT", 6379, 1, 65535));
  c.http_threads = static_cast<std::size_t>(env_int("HTTP_THREADS", 0, 0, 512));
  c.http_worker_threads = static_cast<std::size_t>(env_int("HTTP_WORKER_THREADS", 32, 1, 1024));
  c.db_pool_size = static_cast<std::size_t>(env_int("DB_POOL_SIZE", 8, 1, 256));
  c.seed_on_empty = env_bool("SEED_ON_EMPTY", true);
  c.seed_dir = env_or("SEED_DIR", "data/stackoverflow");
  c.consumer_name = env_or("CONSUMER_NAME", infra::default_consumer_name());
  return c;
}

const std::string& Config::require_database_url() const {
  if (!database_url) {
    throw std::runtime_error("environment variable GRAPH_DATABASE_URL is required");
  }
  return *database_url;
}

const std::string& Config::require_public_key() const {
  if (!public_key_b64) {
    throw std::runtime_error("environment variable AUTH_PUBLIC_KEY is required");
  }
  return *public_key_b64;
}

}  // namespace sg::graph_service
