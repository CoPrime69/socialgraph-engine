// graph-service: profiles and the connection graph (write model, source of truth).
// Subcommands: serve (default), migrate, seed, healthcheck, --help.

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include <httplib.h>

#include <sg/common/auth.hpp>
#include <sg/common/config.hpp>
#include <sg/common/http.hpp>
#include <sg/common/lifecycle.hpp>
#include <sg/common/log.hpp>
#include <sg/common/outbox.hpp>
#include <sg/common/pg.hpp>

#include "sg/graph_service/app/graph_service.hpp"
#include "sg/graph_service/config.hpp"
#include "sg/graph_service/http/read_cache.hpp"
#include "sg/graph_service/http/routes.hpp"
#include "sg/graph_service/infra/graph_events_invalidator.hpp"
#include "sg/graph_service/infra/identity_events_consumer.hpp"
#include "sg/graph_service/infra/migrations.hpp"
#include "sg/graph_service/infra/postgres_graph_repository.hpp"
#include "sg/graph_service/infra/seed_loader.hpp"

namespace {

namespace common = sg::common;
namespace infra = sg::graph_service::infra;
using sg::graph_service::Config;

constexpr std::string_view kUsage = R"(graph-service: user profiles and the connection graph (contracts §5, §10)

Usage: graph-service [serve|migrate|seed|healthcheck|--help]

  serve        migrate, import the dataset into an empty database (SEED_ON_EMPTY), then serve HTTP,
               publish graph.events and consume identity.events (default)
  migrate      apply database migrations and exit
  seed         migrate, import the dataset if the database is empty, and exit
  healthcheck  exit 0 when GET /healthz on 127.0.0.1:$GRAPH_PORT answers 200

Environment:
  GRAPH_PORT              listen port (default 8082)
  GRAPH_DATABASE_URL      PostgreSQL connection URI (required for serve/migrate/seed)
  AUTH_PUBLIC_KEY         identity Ed25519 public key, base64 (required for serve)
  REDIS_HOST / REDIS_PORT event bus (default 127.0.0.1:6379)
  SEED_ON_EMPTY           import SEED_DIR when the database is empty (default true)
  SEED_DIR                directory with categories/branches/users/connections.csv.gz (default data/stackoverflow)
  CONSUMER_NAME           Redis consumer name in group graph-service (default: host name)
  HTTP_THREADS            HTTP event-loop threads (default 0 = one per CPU core)
  HTTP_WORKER_THREADS     threads running request handlers (default 32)
  GRAPH_CACHE_ENTRIES     cached profiles and connection lists (default 100000; 0 disables)
  GRAPH_SEARCH_CACHE_TTL_MS  how long a search page is reused (default 2000; 0 disables)
  DB_POOL_SIZE            PostgreSQL connections (default 8)
  LOG_LEVEL               debug|info|warn|error (default info)
)";

common::PgPool make_pool(const Config& config) {
  common::PgPoolOptions options;
  options.conninfo = config.require_database_url();
  options.size = config.db_pool_size;
  options.application_name = "graph-service";
  return common::PgPool(options);
}

void migrate(common::PgPool& pool) {
  common::run_migrations(pool, infra::embedded_migrations(), infra::kMigrationLockKey);
}

void seed(common::PgPool& pool, const Config& config) {
  const auto report = infra::seed_dataset_if_empty(pool, config.seed_dir);
  if (!report.loaded) {
    common::log_info("dataset import skipped: the graph is not empty");
    return;
  }
  common::log_info("dataset imported", {{"dir", config.seed_dir},
                                        {"categories", report.categories},
                                        {"branches", report.branches},
                                        {"users", report.users},
                                        {"connections", report.connections},
                                        {"demo_connections", report.demo_connections},
                                        {"graph_version", report.graph_version},
                                        {"seconds", report.seconds}});
}

int serve(const Config& config) {
  const common::TokenVerifier verifier(config.require_public_key());
  auto pool = make_pool(config);
  migrate(pool);
  if (config.seed_on_empty) {
    seed(pool, config);
  }

  infra::PostgresGraphRepository repository(pool);
  sg::graph_service::app::GraphService graph(repository);

  common::OutboxRelay relay(pool, config.redis, "graph.events");
  relay.start();

  infra::IdentityEventsConsumer::Options consumer_options;
  consumer_options.consumer = config.consumer_name;
  infra::IdentityEventsConsumer consumer(graph, config.redis, consumer_options);
  consumer.start();

  sg::graph_service::http::ReadCacheOptions cache_options;
  cache_options.member_entries = config.cache_entries;
  cache_options.search_entries = config.search_cache_ttl_ms > 0 ? 20'000 : 0;
  cache_options.search_ttl = std::chrono::milliseconds(config.search_cache_ttl_ms);
  sg::graph_service::http::ReadCache cache(cache_options);
  infra::GraphEventsInvalidator invalidator(cache, config.redis, {});
  invalidator.start();

  common::HttpServerOptions options;
  options.port = config.port;
  options.threads = config.http_threads;
  options.worker_threads = config.http_worker_threads;  // handlers query PostgreSQL
  common::HttpServer server("graph-service", options);
  sg::graph_service::http::register_routes(server, graph, verifier, cache);
  server.set_readiness_check([&pool] { return pool.ping(); });

  std::atomic<bool> listen_failed{false};
  std::thread http([&] {
    try {
      server.listen();
    } catch (const std::exception& e) {
      common::log_error("http server failed", {{"error", e.what()}});
      listen_failed = true;
      common::request_shutdown();
    }
  });
  if (server.wait_until_ready()) {
    common::log_info("graph-service started", {{"port", options.port}, {"consumer", config.consumer_name}});
  }
  const int signal = common::wait_for_shutdown_signal();
  common::log_info("shutting down", {{"signal", signal}});
  server.stop();
  http.join();
  consumer.stop();
  invalidator.stop();
  relay.stop();
  return listen_failed ? 1 : 0;
}

int healthcheck() {
  const int port = static_cast<int>(common::env_int("GRAPH_PORT", 8082, 1, 65535));
  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(2, 0);
  const auto response = client.Get("/healthz");
  return response && response->status == 200 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  common::block_shutdown_signals();
  const std::string_view command = argc > 1 ? argv[1] : "serve";
  if (argc > 2) {
    std::cerr << kUsage;
    return 2;
  }
  if (command == "--help" || command == "-h" || command == "help") {
    std::cout << kUsage;
    return 0;
  }
  try {
    if (command == "healthcheck") {
      return healthcheck();
    }
    common::init_logging("graph-service", common::parse_log_level(common::env_or("LOG_LEVEL", "info")));
    const Config config = Config::from_env();
    if (command == "serve") {
      return serve(config);
    }
    if (command == "migrate" || command == "seed") {
      auto pool = make_pool(config);
      migrate(pool);
      common::log_info("migrations applied");
      if (command == "seed") {
        seed(pool, config);
      }
      return 0;
    }
    std::cerr << kUsage;
    return 2;
  } catch (const std::exception& e) {
    common::log_error("fatal error", {{"error", e.what()}});
    return 1;
  }
}
